#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <base64.h>
#include <time.h>
#include <vector>

#include "Config.h"
#include "PortalHtml.h"

struct Train {
  String Destination;
  int Min;
  String Line;
};

static const int MIN_NONE = -9999;
static const char *TZ_INFO = "EST5EDT,M3.2.0,M11.1.0";
static const char *HOSTNAME = "SplitflapBoard";
static const int MAX_CONSECUTIVE_FAILURES = 10;
static const unsigned long PORTAL_HOLD_MS = 3000;
static const int PORTAL_BUTTON_PIN = 0;

// State

static WebServer server(80);
static volatile uint32_t featureGen = 0;
static String lastRow1 = "";
static String lastRow2 = "";
static int consecutiveFailures = 0;
static String spotifyAccessToken = "";
static unsigned long spotifyTokenExpiry = 0;

void startConfigPortal(bool onDemand);

// Helper Functions

String padLeft(const String &text, int width) {
  String result = text;
  while ((int)result.length() < width) {
    result += " ";
  }
  return result.substring(0, width);
}

String padCenter(const String &text, int width) {
  if ((int)text.length() >= width) {
    return text.substring(0, width);
  }
  String result = "";
  int left = (width - text.length()) / 2;
  for (int i = 0; i < left; i++) {
    result += " ";
  }
  return padLeft(result + text, width);
}

String stripParentheses(String text) {
  while (true) {
    int start = text.indexOf('(');
    if (start < 0) {
      break;
    }
    int end = text.indexOf(')', start);
    if (end < 0) {
      break;
    }
    text = text.substring(0, start) + text.substring(end + 1);
  }
  text.trim();
  return text;
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  return s;
}

int minToInt(const String &minValue) {
  if (minValue == "ARR" || minValue == "BRD") {
    return 0;
  }
  if (minValue == "---" || minValue == "") {
    return MIN_NONE;
  }
  return minValue.toInt();
}

String abbreviateStation(const String &name) {
  auto it = AbrvStations.find(name);
  if (it != AbrvStations.end()) {
    return it->second;
  }
  return name;
}

// Display Output

void showRows(const String &r1, const String &r2) {
  lastRow1 = r1;
  lastRow2 = r2;
  Serial.println(r1);
  Serial.println(r2);
}

String formatTimeLine() {
  struct tm now;
  if (!getLocalTime(&now, 2000)) {
    return padCenter("--:--", BOARD_COLS);
  }
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &now);
  return padCenter(String(buf), BOARD_COLS);
}

void printTrain(const Train &train) {
  String text = train.Destination + " " + String(train.Min) + " " + train.Line;
  showRows(formatTimeLine(), padLeft(text, BOARD_COLS));
}

void printMessage(const String &message) {
  showRows(formatTimeLine(), padLeft(message, BOARD_COLS));
}

void smartDelay(unsigned long ms) {
  uint32_t gen = featureGen;
  unsigned long start = millis();
  unsigned long buttonDownAt = 0;
  while (millis() - start < ms) {
    ArduinoOTA.handle();
    server.handleClient();
    if (featureGen != gen) {
      return;
    }
    if (digitalRead(PORTAL_BUTTON_PIN) == LOW) {
      if (buttonDownAt == 0) {
        buttonDownAt = millis();
      } else if (millis() - buttonDownAt >= PORTAL_HOLD_MS) {
        startConfigPortal(true);
        return;
      }
    } else {
      buttonDownAt = 0;
    }
    delay(10);
  }
}

// Wifi Setup / Portal

static WiFiManagerParameter paramApiKey("apikey", "WMATA API key", "", 64);
static WiFiManagerParameter paramStation("station", "Station code (e.g. A01)",
                                         "", 8);
static WiFiManagerParameter
    paramThreshold("thresh", "Minute threshold (walk time)", "", 4);
static WiFiManagerParameter paramRefresh("refresh", "Refresh seconds", "", 6);

void applyPortalValues() {
  String v;
  v = paramApiKey.getValue();
  if (v.length() > 0) {
    AppSettings.apiKey = v;
  }
  v = paramStation.getValue();
  if (v.length() > 0) {
    AppSettings.stationCode = v;
  }
  v = paramThreshold.getValue();
  if (v.length() > 0) {
    AppSettings.minuteThreshold = v.toInt();
  }
  v = paramRefresh.getValue();
  if (v.length() > 0 && v.toInt() > 0) {
    AppSettings.refreshSeconds = v.toInt();
  }
  saveSettings();
}

void startConfigPortal(bool onDemand) {
  WiFiManager wm;
  wm.addParameter(&paramApiKey);
  wm.addParameter(&paramStation);
  wm.addParameter(&paramThreshold);
  wm.addParameter(&paramRefresh);
  wm.setSaveParamsCallback(applyPortalValues);
  wm.setConfigPortalTimeout(180);
  if (onDemand) {
    Serial.println("Opening config portal (AP: SplitflapBoard)...");
    wm.startConfigPortal(HOSTNAME);
  } else {
    Serial.println("Connecting to WiFi (portal opens if this fails)...");
    if (!wm.autoConnect(HOSTNAME)) {
      Serial.println("Portal timed out without config, restarting");
      ESP.restart();
    }
  }
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
}

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  Serial.print("WiFi lost, reconnecting");
  WiFi.disconnect();
  WiFi.reconnect();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Reconnected");
    return true;
  }
  Serial.println("Reconnect failed");
  return false;
}

// Wmata

bool isMetroOpen() {
  struct tm now;
  if (!getLocalTime(&now, 2000) || now.tm_year + 1900 < 2020) {
    return true;
  }
  int minutes = now.tm_hour * 60 + now.tm_min;
  int wday = now.tm_wday;
  if (minutes < 60) {
    return wday == 6 || wday == 0;
  }
  int openAt = (wday >= 1 && wday <= 5) ? 5 * 60 : 7 * 60;
  return minutes >= openAt;
}

bool fetchWmataJson(const String &url, JsonDocument &doc) {
  WiFiClientSecure client;
  client.setCACert(WMATA_ROOT_CA);
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("api_key", AppSettings.apiKey.c_str());
  int statusCode = http.GET();
  if (statusCode != 200) {
    Serial.print("HTTP error: ");
    Serial.println(statusCode);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }
  return true;
}

bool getUpcomingTrains(const String &stationCode, int threshold,
                       std::vector<Train> &upcomingTrains) {
  JsonDocument data;
  String url =
      "https://api.wmata.com/StationPrediction.svc/json/GetPrediction/" +
      stationCode;
  if (!fetchWmataJson(url, data) || !data["Trains"].is<JsonArray>()) {
    Serial.println("No trains found or API error");
    return false;
  }
  for (JsonObject train : data["Trains"].as<JsonArray>()) {
    String destination = train["Destination"].as<String>();
    bool alreadyAdded = false;
    for (const Train &saved : upcomingTrains) {
      if (saved.Destination == destination ||
          saved.Destination == abbreviateStation(destination)) {
        alreadyAdded = true;
        break;
      }
    }
    if (alreadyAdded) {
      continue;
    }
    int minutes = minToInt(train["Min"].as<String>());
    if (minutes != MIN_NONE && minutes >= threshold) {
      Train entry;
      entry.Destination = abbreviateStation(destination);
      entry.Min = minutes;
      entry.Line = train["Line"].as<String>();
      upcomingTrains.push_back(entry);
    }
  }
  return true;
}

void showTrainCycle(const std::vector<Train> &upcomingTrains) {
  size_t size = upcomingTrains.size();
  unsigned long cycleMs = AppSettings.refreshSeconds * 1000UL;
  if (size == 0) {
    printMessage("NO TRAINS");
    smartDelay(cycleMs);
    return;
  }
  unsigned long delayPerTrain = cycleMs / size;
  for (size_t i = 0; i < size; i++) {
    printTrain(upcomingTrains[i]);
    smartDelay(delayPerTrain);
  }
}

// Spotify

bool refreshSpotifyToken() {
  if (AppSettings.spotifyId.length() == 0 ||
      AppSettings.spotifyRefresh.length() == 0) {
    return false;
  }
  if (spotifyAccessToken.length() > 0 && millis() < spotifyTokenExpiry) {
    return true;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Authorization",
                 "Basic " + base64::encode(AppSettings.spotifyId + ":" +
                                           AppSettings.spotifySecret));
  int code = http.POST("grant_type=refresh_token&refresh_token=" +
                       AppSettings.spotifyRefresh);
  if (code != 200) {
    Serial.print("Spotify token error: ");
    Serial.println(code);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    return false;
  }
  spotifyAccessToken = doc["access_token"].as<String>();
  unsigned long expiresIn = doc["expires_in"] | 3600UL;
  spotifyTokenExpiry = millis() + (expiresIn - 60) * 1000UL;
  return spotifyAccessToken.length() > 0;
}

bool getNowPlaying(String &song, String &artist, bool &playing) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://api.spotify.com/v1/me/player/currently-playing");
  http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
  int code = http.GET();
  if (code == 204) {
    playing = false;
    http.end();
    return true;
  }
  if (code != 200) {
    Serial.print("Spotify API error: ");
    Serial.println(code);
    http.end();
    if (code == 401) {
      spotifyAccessToken = "";
    }
    return false;
  }
  JsonDocument filter;
  filter["is_playing"] = true;
  filter["item"]["name"] = true;
  filter["item"]["artists"][0]["name"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(
      doc, http.getString(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    return false;
  }
  playing = doc["is_playing"] | false;
  song = doc["item"]["name"].as<String>();
  artist = doc["item"]["artists"][0]["name"].as<String>();
  return true;
}

void runTrainsFeature() {
  if (!isMetroOpen()) {
    printMessage("METRO CLOSED");
    smartDelay(60000);
    return;
  }
  if (AppSettings.apiKey.length() == 0) {
    printMessage("NO API KEY");
    smartDelay(10000);
    return;
  }
  std::vector<Train> upcoming;
  bool ok = getUpcomingTrains(AppSettings.stationCode,
                              AppSettings.minuteThreshold, upcoming);
  if (ok) {
    consecutiveFailures = 0;
  } else {
    consecutiveFailures++;
    if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
      Serial.println("Too many failures, restarting");
      ESP.restart();
    }
  }
  showTrainCycle(upcoming);
}

void runClockFeature() {
  struct tm now;
  String date = "";
  if (getLocalTime(&now, 2000)) {
    char buf[16];
    strftime(buf, sizeof(buf), "%a %b %d", &now);
    date = String(buf);
    date.toUpperCase();
  }
  showRows(formatTimeLine(), padCenter(date, BOARD_COLS));
  smartDelay(10000);
}

void runTextFeature() {
  if (AppSettings.textRow1.length() == 0 &&
      AppSettings.textRow2.length() == 0) {
    showRows(formatTimeLine(), padCenter("ENTER TEXT", BOARD_COLS));
  } else {
    showRows(padCenter(AppSettings.textRow1, BOARD_COLS),
             padCenter(AppSettings.textRow2, BOARD_COLS));
  }
  smartDelay(10000);
}

void runSpotifyFeature() {
  if (AppSettings.spotifyId.length() == 0 ||
      AppSettings.spotifyRefresh.length() == 0) {
    printMessage("SPOTIFY SETUP");
    smartDelay(10000);
    return;
  }
  if (!refreshSpotifyToken()) {
    printMessage("SPOTIFY AUTH ERR");
    smartDelay(15000);
    return;
  }
  String song = "";
  String artist = "";
  bool playing = false;
  if (!getNowPlaying(song, artist, playing)) {
    printMessage("SPOTIFY ERR");
    smartDelay(15000);
    return;
  }
  if (!playing || song.length() == 0) {
    printMessage("NOT PLAYING");
    smartDelay(10000);
    return;
  }
  song = stripParentheses(song);
  showRows(padLeft(song, BOARD_COLS), padLeft(artist, BOARD_COLS));
  smartDelay(5000);
}

// Web Server Endpoints

void handleStatus() {
  String j = "{\"feature\":\"" + AppSettings.activeFeature + "\",\"row1\":\"" +
             jsonEscape(lastRow1) + "\",\"row2\":\"" + jsonEscape(lastRow2) +
             "\"}";
  server.send(200, "application/json", j);
}

void handleConfig() {
  // Report presence of secrets without exposing their values.
  String j = "{\"apikey\":" +
             String(AppSettings.apiKey.length() > 0 ? "true" : "false") +
             ",\"spotify\":" +
             String((AppSettings.spotifyId.length() > 0 &&
                     AppSettings.spotifyRefresh.length() > 0)
                        ? "true"
                        : "false") +
             ",\"station\":\"" + jsonEscape(AppSettings.stationCode) +
             "\",\"thresh\":" + String(AppSettings.minuteThreshold) +
             ",\"refresh\":" + String(AppSettings.refreshSeconds) +
             ",\"text1\":\"" + jsonEscape(AppSettings.textRow1) +
             "\",\"text2\":\"" + jsonEscape(AppSettings.textRow2) + "\"}";
  server.send(200, "application/json", j);
}

void handleSetText() {
  AppSettings.textRow1 = server.arg("row1");
  AppSettings.textRow2 = server.arg("row2");
  AppSettings.activeFeature = "text";
  saveSettings();
  featureGen++;
  server.send(200, "text/plain", "ok");
}

void handleListPresets() {
  String j = "[";
  bool first = true;
  for (int i = 0; i < MAX_TEXT_PRESETS; i++) {
    if (TextPresets[i].length() == 0) {
      continue;
    }
    int nl = TextPresets[i].indexOf('\n');
    String r1 = nl >= 0 ? TextPresets[i].substring(0, nl) : TextPresets[i];
    String r2 = nl >= 0 ? TextPresets[i].substring(nl + 1) : "";
    if (!first) {
      j += ",";
    }
    first = false;
    j += "{\"slot\":" + String(i) + ",\"r1\":\"" + jsonEscape(r1) +
         "\",\"r2\":\"" + jsonEscape(r2) + "\"}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

void handlePresetAction() {
  String action = server.arg("action");
  if (action == "save") {
    String r1 = server.arg("row1");
    String r2 = server.arg("row2");
    if (r1.length() == 0 && r2.length() == 0) {
      server.send(400, "text/plain", "empty");
      return;
    }
    for (int i = 0; i < MAX_TEXT_PRESETS; i++) {
      if (TextPresets[i].length() == 0) {
        TextPresets[i] = r1 + "\n" + r2;
        saveSettings();
        server.send(200, "text/plain", "ok");
        return;
      }
    }
    server.send(400, "text/plain", "full");
  } else if (action == "delete") {
    int slot = server.arg("slot").toInt();
    if (slot < 0 || slot >= MAX_TEXT_PRESETS) {
      server.send(400, "text/plain", "bad slot");
      return;
    }

    for (int i = slot; i < MAX_TEXT_PRESETS - 1; i++) {
      TextPresets[i] = TextPresets[i + 1];
    }
    TextPresets[MAX_TEXT_PRESETS - 1] = "";
    saveSettings();
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "unknown action");
  }
}

void handleSetFeature() {
  String n = server.arg("name");
  if (n == "trains" || n == "clock" || n == "spotify" || n == "text") {
    AppSettings.activeFeature = n;
    saveSettings();
    featureGen++;
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "unknown feature");
  }
}

void handleSaveTrains() {
  String v = server.arg("apikey");
  v.trim();
  if (v.length() > 0) {
    AppSettings.apiKey = v;
    consecutiveFailures = 0;
  }
  v = server.arg("station");
  if (v.length() > 0) {
    AppSettings.stationCode = v;
  }
  v = server.arg("thresh");
  if (v.length() > 0) {
    AppSettings.minuteThreshold = v.toInt();
  }
  v = server.arg("refresh");
  if (v.length() > 0 && v.toInt() > 0) {
    AppSettings.refreshSeconds = v.toInt();
  }
  saveSettings();
  featureGen++;
  server.send(200, "text/plain", "ok");
}

void handleSaveSpotify() {
  String v = server.arg("id");
  if (v.length() > 0) {
    AppSettings.spotifyId = v;
  }
  v = server.arg("secret");
  if (v.length() > 0) {
    AppSettings.spotifySecret = v;
  }
  v = server.arg("token");
  if (v.length() > 0) {
    AppSettings.spotifyRefresh = v;
  }
  spotifyAccessToken = "";
  spotifyTokenExpiry = 0;
  saveSettings();
  featureGen++;
  server.send(200, "text/plain", "ok");
}

void setupWebServer() {
  server.on("/", HTTP_GET,
            []() { server.send_P(200, "text/html", PORTAL_HTML); });
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/text", HTTP_POST, handleSetText);
  server.on("/presets", HTTP_GET, handleListPresets);
  server.on("/preset", HTTP_POST, handlePresetAction);
  server.on("/feature", HTTP_POST, handleSetFeature);
  server.on("/trains", HTTP_POST, handleSaveTrains);
  server.on("/spotify", HTTP_POST, handleSaveSpotify);
  server.begin();
  MDNS.addService("http", "tcp", 80);
}

// Arduino

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(PORTAL_BUTTON_PIN, INPUT_PULLUP);
  loadSettings();
  startConfigPortal(false);
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.begin();
  setupWebServer();
  Serial.println("OTA ready (hostname: SplitflapBoard)");
  Serial.print("Portal: http://");
  Serial.println(WiFi.localIP());
  Serial.println("Portal: http://SplitflapBoard.local");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  if (!ensureWiFi()) {
    consecutiveFailures++;
    if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
      Serial.println("Too many failures, restarting");
      ESP.restart();
    }
    smartDelay(5000);
    return;
  }
  if (AppSettings.activeFeature == "clock") {
    runClockFeature();
  } else if (AppSettings.activeFeature == "spotify") {
    runSpotifyFeature();
  } else if (AppSettings.activeFeature == "text") {
    runTextFeature();
  } else {
    runTrainsFeature();
  }
}
