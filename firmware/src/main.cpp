#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <set>
#include <vector>

#include "Config.h"

struct Train
{
  String Destination;
  int Min;
  String Line;
};

static const int MIN_NONE = -9999;

// Eastern time with DST rules, for Metro service hours.
static const char *TZ_INFO = "EST5EDT,M3.2.0,M11.1.0";

// How many consecutive failed API/WiFi cycles before rebooting.
static const int MAX_CONSECUTIVE_FAILURES = 10;

// Re-check incidents at most every 5 minutes to save API quota.
static const unsigned long INCIDENT_INTERVAL_MS = 5UL * 60UL * 1000UL;

// Hold BOOT (GPIO0) this long to reopen the config portal.
static const unsigned long PORTAL_HOLD_MS = 3000;
static const int PORTAL_BUTTON_PIN = 0;

static int consecutiveFailures = 0;
static unsigned long lastIncidentFetch = 0;
static std::vector<String> activeAlerts;
static std::set<String> stationLines;

// Forward declarations
void startConfigPortal(bool onDemand);

// ---------------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------------

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

// Delay that keeps OTA and the portal button responsive while waiting.
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

// ---------------------------------------------------------------------------
// WiFi + config portal
// ---------------------------------------------------------------------------

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
    // Connects with stored credentials, or opens the AP if none work.
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

// Returns true when connected, reconnecting with a timeout if needed.
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

// ---------------------------------------------------------------------------
// Metro service hours (NTP-synced Eastern time)
// ---------------------------------------------------------------------------

// Weekday service: Mon-Thu 5:00-24:00, Fri 5:00-1:00, Sat 7:00-1:00,
// Sun 7:00-24:00. Fails open when the clock has not synced yet.
bool isMetroOpen()
{
  struct tm now;
  if (!getLocalTime(&now, 2000) || now.tm_year + 1900 < 2020)
  {
    return true;
  }

  int minutes = now.tm_hour * 60 + now.tm_min;
  int wday = now.tm_wday; // 0 = Sunday

  // 00:00-01:00 is carryover from Friday (wday 6) or Saturday (wday 0) service.
  if (minutes < 60)
  {
    return wday == 6 || wday == 0;
  }

  int openAt = (wday >= 1 && wday <= 5) ? 5 * 60 : 7 * 60;
  return minutes >= openAt;
}

// ---------------------------------------------------------------------------
// WMATA API
// ---------------------------------------------------------------------------

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

// Fills upcomingTrains; returns false on API failure (distinct from
// a successful call that simply has no arrivals).
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

      String line = train["Line"].as<String>();
      if (line.length() > 0 && line != "--")
      {
        stationLines.insert(line);
      }
    }
  }

  return true;
}

// Pulls the Incidents API and keeps alerts for lines serving our station
// (or all alerts if we have not seen any predictions yet).
void RefreshIncidents()
{
  if (lastIncidentFetch != 0 && millis() - lastIncidentFetch < INCIDENT_INTERVAL_MS)
  {
    return;
  }
  lastIncidentFetch = millis();

  JsonDocument data;
  if (!FetchWmataJson("https://api.wmata.com/Incidents.svc/json/Incidents", data) ||
      !data["Incidents"].is<JsonArray>())
  {
    return;
  }

  activeAlerts.clear();
  std::set<String> alerted;

  for (JsonObject incident : data["Incidents"].as<JsonArray>())
  {
    String lines = incident["LinesAffected"].as<String>(); // e.g. "RD;" or "OR; SV;"
    String type = incident["IncidentType"].as<String>();   // e.g. "Delay", "Alert"
    type.toUpperCase();

    int start = 0;
    while (start < (int)lines.length())
    {
      int sep = lines.indexOf(';', start);
      if (sep < 0)
      {
        sep = lines.length();
      }
      String code = lines.substring(start, sep);
      code.trim();
      start = sep + 1;

      if (code.length() == 0)
      {
        continue;
      }
      if (!stationLines.empty() && stationLines.find(code) == stationLines.end())
      {
        continue;
      }

      String alert = code + " LINE " + type;
      if (alerted.insert(alert).second)
      {
        activeAlerts.push_back(alert);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

void PrintSpacer(const std::vector<Train> &upcomingTrains)
{
  size_t size = upcomingTrains.size();
  unsigned long cycleMs = AppSettings.refreshSeconds * 1000UL;

  for (const String &alert : activeAlerts)
  {
    Serial.println(alert);
  }

  if (size == 0)
  {
    smartDelay(cycleMs);
    return;
  }

  unsigned long delayPerTrain = cycleMs / size;

  for (size_t i = 0; i < size; i++)
  {
    Serial.print(upcomingTrains[i].Destination);
    Serial.print("  ");
    Serial.print(upcomingTrains[i].Min);
    Serial.print("  ");
    Serial.println(upcomingTrains[i].Line);

    smartDelay(delayPerTrain);
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

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
    Serial.println("METRO CLOSED");
    smartDelay(60000);
    return;
  }

  if (AppSettings.apiKey.length() == 0)
  {
    Serial.println("No API key set - hold BOOT for 3s to open the config portal");
    smartDelay(10000);
    return;
  }

  RefreshIncidents();

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
