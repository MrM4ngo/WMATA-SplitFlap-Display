#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <vector>

#include "Config.h"

struct Train
{
  String Destination;
  int Min;
  String Line;
};

static const int MIN_NONE = -9999;
static const char *TZ_INFO = "EST5EDT,M3.2.0,M11.1.0";
static const int MAX_CONSECUTIVE_FAILURES = 10;
static const unsigned long PORTAL_HOLD_MS = 3000;
static const int PORTAL_BUTTON_PIN = 0;

static int consecutiveFailures = 0;

void startConfigPortal(bool onDemand);

int MinToInt(const String &MinValue)
{
  if (MinValue == "ARR" || MinValue == "BRD")
  {
    return 0;
  }
  if (MinValue == "---" || MinValue == "")
  {
    return MIN_NONE;
  }
  return MinValue.toInt();
}

String AbbreviateChecker(const String &name)
{
  auto it = AbrvStations.find(name);
  if (it != AbrvStations.end())
  {
    return it->second;
  }
  return name;
}

void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  unsigned long buttonDownAt = 0;
  while (millis() - start < ms)
  {
    ArduinoOTA.handle();
    if (digitalRead(PORTAL_BUTTON_PIN) == LOW)
    {
      if (buttonDownAt == 0)
      {
        buttonDownAt = millis();
      }
      else if (millis() - buttonDownAt >= PORTAL_HOLD_MS)
      {
        startConfigPortal(true);
        return;
      }
    }
    else
    {
      buttonDownAt = 0;
    }
    delay(10);
  }
}

static WiFiManagerParameter paramApiKey("apikey", "WMATA API key", "", 64);
static WiFiManagerParameter paramStation("station", "Station code (e.g. A01)", "", 8);
static WiFiManagerParameter paramThreshold("thresh", "Minute threshold (walk time)", "", 4);
static WiFiManagerParameter paramRefresh("refresh", "Refresh seconds", "", 6);

void applyPortalValues()
{
  String v;
  v = paramApiKey.getValue();
  if (v.length() > 0)
  {
    AppSettings.apiKey = v;
  }
  v = paramStation.getValue();
  if (v.length() > 0)
  {
    AppSettings.stationCode = v;
  }
  v = paramThreshold.getValue();
  if (v.length() > 0)
  {
    AppSettings.minuteThreshold = v.toInt();
  }
  v = paramRefresh.getValue();
  if (v.length() > 0 && v.toInt() > 0)
  {
    AppSettings.refreshSeconds = v.toInt();
  }
  saveSettings();
}

void startConfigPortal(bool onDemand)
{
  WiFiManager wm;
  wm.addParameter(&paramApiKey);
  wm.addParameter(&paramStation);
  wm.addParameter(&paramThreshold);
  wm.addParameter(&paramRefresh);
  wm.setSaveParamsCallback(applyPortalValues);
  wm.setConfigPortalTimeout(180);
  if (onDemand)
  {
    Serial.println("Opening config portal (AP: WMATA-Setup)...");
    wm.startConfigPortal("WMATA-Setup");
  }
  else
  {
    Serial.println("Connecting to WiFi (portal opens if this fails)...");
    if (!wm.autoConnect("WMATA-Setup"))
    {
      Serial.println("Portal timed out without config, restarting");
      ESP.restart();
    }
  }
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
}

bool ensureWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return true;
  }
  Serial.print("WiFi lost, reconnecting");
  WiFi.disconnect();
  WiFi.reconnect();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
  {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Reconnected");
    return true;
  }
  Serial.println("Reconnect failed");
  return false;
}

bool isMetroOpen()
{
  struct tm now;
  if (!getLocalTime(&now, 2000) || now.tm_year + 1900 < 2020)
  {
    return true;
  }
  int minutes = now.tm_hour * 60 + now.tm_min;
  int wday = now.tm_wday;
  if (minutes < 60)
  {
    return wday == 6 || wday == 0;
  }
  int openAt = (wday >= 1 && wday <= 5) ? 5 * 60 : 7 * 60;
  return minutes >= openAt;
}

bool FetchWmataJson(const String &url, JsonDocument &doc)
{
  WiFiClientSecure client;
  client.setCACert(WMATA_ROOT_CA);
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("api_key", AppSettings.apiKey.c_str());
  int statusCode = http.GET();
  if (statusCode != 200)
  {
    Serial.print("HTTP error: ");
    Serial.println(statusCode);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  DeserializationError err = deserializeJson(doc, payload);
  if (err)
  {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }
  return true;
}

bool TrainPredictions(const String &StationCode, JsonDocument &doc)
{
  String url = "https://api.wmata.com/StationPrediction.svc/json/GetPrediction/" + StationCode;
  return FetchWmataJson(url, doc);
}

bool GetUpcomingTrains(const String &StationCode, int Threshold, std::vector<Train> &upcomingTrains)
{
  JsonDocument data;
  if (!TrainPredictions(StationCode, data) || !data["Trains"].is<JsonArray>())
  {
    Serial.println("No trains found or API error");
    return false;
  }
  JsonArray trains = data["Trains"].as<JsonArray>();
  for (JsonObject train : trains)
  {
    String destination = train["Destination"].as<String>();
    bool alreadyAdded = false;
    for (const Train &saved : upcomingTrains)
    {
      if (saved.Destination == destination || saved.Destination == AbbreviateChecker(destination))
      {
        alreadyAdded = true;
        break;
      }
    }
    if (alreadyAdded)
    {
      continue;
    }
    String minStr = train["Min"].as<String>();
    int minutes = MinToInt(minStr);
    if (minutes != MIN_NONE && minutes >= Threshold)
    {
      Train tempTrain;
      tempTrain.Destination = AbbreviateChecker(destination);
      tempTrain.Min = minutes;
      tempTrain.Line = train["Line"].as<String>();
      upcomingTrains.push_back(tempTrain);
    }
  }
  return true;
}

String padCenter(const String &text, int width)
{
  if ((int)text.length() >= width)
  {
    return text.substring(0, width);
  }
  int pad = width - text.length();
  int left = pad / 2;
  String result = "";
  for (int i = 0; i < left; i++)
  {
    result += " ";
  }
  result += text;
  while ((int)result.length() < width)
  {
    result += " ";
  }
  return result.substring(0, width);
}

String padLeft(const String &text, int width)
{
  String result = text;
  while ((int)result.length() < width)
  {
    result += " ";
  }
  return result.substring(0, width);
}

String formatTimeLine()
{
  struct tm now;
  if (!getLocalTime(&now, 2000))
  {
    return padCenter("--:--", BOARD_COLS);
  }
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &now);
  return padCenter(String(buf), BOARD_COLS);
}

String formatTrainLine(const String &destination, int min, const String &line)
{
  String text = destination + " " + String(min) + " " + line;
  return padLeft(text, BOARD_COLS);
}

String formatTrainLine(const Train &train)
{
  return formatTrainLine(train.Destination, train.Min, train.Line);
}

void printDisplay(const String &destination, int min, const String &line)
{
  Serial.println(formatTimeLine());
  Serial.println(formatTrainLine(destination, min, line));
}

void printDisplay(const Train &train)
{
  printDisplay(train.Destination, train.Min, train.Line);
}

void printMessage(const String &message)
{
  Serial.println(formatTimeLine());
  Serial.println(padLeft(message, BOARD_COLS));
}

void PrintSpacer(const std::vector<Train> &upcomingTrains)
{
  size_t size = upcomingTrains.size();
  unsigned long cycleMs = AppSettings.refreshSeconds * 1000UL;
  if (size == 0)
  {
    printMessage("NO TRAINS");
    smartDelay(cycleMs);
    return;
  }
  unsigned long delayPerTrain = cycleMs / size;
  for (size_t i = 0; i < size; i++)
  {
    printDisplay(upcomingTrains[i]);
    smartDelay(delayPerTrain);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  pinMode(PORTAL_BUTTON_PIN, INPUT_PULLUP);
  loadSettings();
  startConfigPortal(false);
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
  ArduinoOTA.setHostname("wmata-splitflap");
  ArduinoOTA.begin();
  Serial.println("OTA ready (hostname: wmata-splitflap)");
}

void loop()
{
  ArduinoOTA.handle();
  if (!ensureWiFi())
  {
    consecutiveFailures++;
    if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES)
    {
      Serial.println("Too many failures, restarting");
      ESP.restart();
    }
    smartDelay(5000);
    return;
  }
  if (!isMetroOpen())
  {
    printMessage("METRO CLOSED");
    smartDelay(60000);
    return;
  }
  if (AppSettings.apiKey.length() == 0)
  {
    Serial.println("No API key set - hold BOOT for 3s to open the config portal");
    smartDelay(10000);
    return;
  }
  std::vector<Train> upcoming;
  bool ok = GetUpcomingTrains(AppSettings.stationCode, AppSettings.minuteThreshold, upcoming);
  if (ok)
  {
    consecutiveFailures = 0;
  }
  else
  {
    consecutiveFailures++;
    if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES)
    {
      Serial.println("Too many failures, restarting");
      ESP.restart();
    }
  }
  PrintSpacer(upcoming);
}
