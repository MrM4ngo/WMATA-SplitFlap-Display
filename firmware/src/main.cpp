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

struct Train {
  String Destination;
  int Min;
  String Line;
};

static const int MIN_NONE = -9999;
static const char *TZ_INFO = "EST5EDT,M3.2.0,M11.1.0";
static const int MAX_CONSECUTIVE_FAILURES = 10;
static const unsigned long PORTAL_HOLD_MS = 3000;
static const int PORTAL_BUTTON_PIN = 0;

static WebServer server(80);
static volatile uint32_t featureGen = 0;
static String lastRow1 = "";
static String lastRow2 = "";
static int consecutiveFailures = 0;
static String spotifyAccessToken = "";
static unsigned long spotifyTokenExpiry = 0;

void startConfigPortal(bool onDemand);

int MinToInt(const String &MinValue) {
  if (MinValue == "ARR" || MinValue == "BRD") {
    return 0;
  }
  if (MinValue == "---" || MinValue == "") {
    return MIN_NONE;
  }
  return MinValue.toInt();
}

String AbbreviateChecker(const String &name) {
  auto it = AbrvStations.find(name);
  if (it != AbrvStations.end()) {
    return it->second;
  }
  return name;
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
    Serial.println("Opening config portal (AP: SplitflapBoard-Setup)...");
    wm.startConfigPortal("SplitflapBoard");
  } else {
    Serial.println("Connecting to WiFi (portal opens if this fails)...");
    if (!wm.autoConnect("SplitflapBoard")) {
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

String padCenter(const String &text, int width) {
  if ((int)text.length() >= width) {
    return text.substring(0, width);
  }
  int pad = width - text.length();
  int left = pad / 2;
  String result = "";
  for (int i = 0; i < left; i++) {
    result += " ";
  }
  result += text;
  while ((int)result.length() < width) {
    result += " ";
  }
  return result.substring(0, width);
}

String padLeft(const String &text, int width) {
  String result = text;
  while ((int)result.length() < width) {
    result += " ";
  }
  return result.substring(0, width);
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

String formatTimeLine() {
  struct tm now;
  if (!getLocalTime(&now, 2000)) {
    return padCenter("--:--", BOARD_COLS);
  }
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &now);
  return padCenter(String(buf), BOARD_COLS);
}

void showRows(const String &r1, const String &r2) {
  lastRow1 = r1;
  lastRow2 = r2;
  Serial.println(r1);
  Serial.println(r2);
}

String formatTrainLine(const Train &train) {
  String text = train.Destination + " " + String(train.Min) + " " + train.Line;
  return padLeft(text, BOARD_COLS);
}

void printDisplay(const Train &train) {
  showRows(formatTimeLine(), formatTrainLine(train));
}

void printMessage(const String &message) {
  showRows(formatTimeLine(), padLeft(message, BOARD_COLS));
}

bool FetchWmataJson(const String &url, JsonDocument &doc) {
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

bool TrainPredictions(const String &StationCode, JsonDocument &doc) {
  String url =
      "https://api.wmata.com/StationPrediction.svc/json/GetPrediction/" +
      StationCode;
  return FetchWmataJson(url, doc);
}

bool GetUpcomingTrains(const String &StationCode, int Threshold,
                       std::vector<Train> &upcomingTrains) {
  JsonDocument data;
  if (!TrainPredictions(StationCode, data) || !data["Trains"].is<JsonArray>()) {
    Serial.println("No trains found or API error");
    return false;
  }
  JsonArray trains = data["Trains"].as<JsonArray>();
  for (JsonObject train : trains) {
    String destination = train["Destination"].as<String>();
    bool alreadyAdded = false;
    for (const Train &saved : upcomingTrains) {
      if (saved.Destination == destination ||
          saved.Destination == AbbreviateChecker(destination)) {
        alreadyAdded = true;
        break;
      }
    }
    if (alreadyAdded) {
      continue;
    }
    String minStr = train["Min"].as<String>();
    int minutes = MinToInt(minStr);
    if (minutes != MIN_NONE && minutes >= Threshold) {
      Train tempTrain;
      tempTrain.Destination = AbbreviateChecker(destination);
      tempTrain.Min = minutes;
      tempTrain.Line = train["Line"].as<String>();
      upcomingTrains.push_back(tempTrain);
    }
  }
  return true;
}

void PrintSpacer(const std::vector<Train> &upcomingTrains) {
  size_t size = upcomingTrains.size();
  unsigned long cycleMs = AppSettings.refreshSeconds * 1000UL;
  if (size == 0) {
    printMessage("NO TRAINS");
    smartDelay(cycleMs);
    return;
  }
  unsigned long delayPerTrain = cycleMs / size;
  for (size_t i = 0; i < size; i++) {
    printDisplay(upcomingTrains[i]);
    smartDelay(delayPerTrain);
  }
}

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
  bool ok = GetUpcomingTrains(AppSettings.stationCode,
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
  PrintSpacer(upcoming);
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

static const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Split-Flap Display</title>
<style>
:root{--bg:#0f1115;--card:#171b22;--line:#272d38;--text:#e8eaed;--muted:#98a1ad;--accent:#f5b52e;--green:#2ecc71;--red:#e5484d;--btn:#222834}
*{box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--text);margin:0 auto;padding:20px 16px 72px;max-width:540px}
header{display:flex;align-items:center;justify-content:space-between;margin:0 0 14px}
h1{font-size:19px;font-weight:650;margin:0}
.pill{display:inline-flex;align-items:center;gap:7px;font-size:12px;color:var(--muted);background:var(--card);border:1px solid var(--line);padding:5px 11px;border-radius:999px}
.dot{width:8px;height:8px;border-radius:50%;background:var(--red)}
.dot.on{background:var(--green)}
.board{background:#000;border:1px solid var(--line);border-radius:12px;padding:18px 10px;text-align:center;box-shadow:inset 0 0 28px rgba(245,181,46,.05)}
.board div{font-family:Consolas,"Courier New",monospace;font-size:clamp(13px,4.2vw,20px);letter-spacing:.32em;white-space:pre;color:var(--accent);text-shadow:0 0 9px rgba(245,181,46,.3);line-height:1.7;overflow:hidden}
h2{font-size:13px;font-weight:600;color:var(--muted);margin:0;text-transform:uppercase;letter-spacing:.08em}
.seg{display:flex;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:4px;gap:4px;margin:18px 0 22px}
.seg button{flex:1;padding:11px 0;border:0;border-radius:8px;background:transparent;color:var(--muted);font-size:14px;font-weight:600;cursor:pointer;transition:background .15s,color .15s}
.seg button.active{background:var(--accent);color:#141414}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin:0 0 16px}
.card-h{display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}
.badge{font-size:11px;font-weight:600;padding:3px 9px;border-radius:999px;background:rgba(229,72,77,.12);color:var(--red)}
.badge.ok{background:rgba(46,204,113,.12);color:var(--green)}
label{display:block;font-size:12px;color:var(--muted);margin:10px 0 5px}
input{width:100%;padding:10px 11px;border-radius:8px;border:1px solid var(--line);background:#10141a;color:var(--text);font-size:14px;outline:none;transition:border-color .15s}
input:focus{border-color:var(--accent)}
.grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}
.hint{font-size:11.5px;color:var(--muted);margin:6px 0 0}
.hint a{color:var(--accent);text-decoration:none}
.save{width:100%;margin-top:14px;padding:11px;border:0;border-radius:8px;background:var(--btn);color:var(--text);font-size:14px;font-weight:600;cursor:pointer;transition:background .15s}
.save:hover{background:#2b3342}
.toast{position:fixed;left:50%;bottom:22px;transform:translate(-50%,80px);background:#e8eaed;color:#14171c;font-size:14px;font-weight:600;padding:10px 20px;border-radius:999px;transition:transform .25s;pointer-events:none}
.toast.show{transform:translate(-50%,0)}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.preset{display:flex;align-items:center;gap:8px;background:#10141a;border:1px solid var(--line);border-radius:8px;padding:8px 12px;margin-top:8px;cursor:pointer;transition:border-color .15s}
.preset:hover{border-color:var(--accent)}
.preset .tx{flex:1;font-family:Consolas,"Courier New",monospace;font-size:12.5px;white-space:pre;overflow:hidden;text-overflow:ellipsis}
.preset .del{background:none;border:0;color:var(--muted);font-size:15px;cursor:pointer;padding:2px 6px;border-radius:6px}
.preset .del:hover{color:var(--red)}
.presets-empty{font-size:12px;color:var(--muted);margin-top:10px}
</style></head><body>
<header><h1>Split-Flap Display</h1><span class="pill"><span class="dot" id="dot"></span><span id="conn">Connecting</span></span></header>
<div class="board"><div id="r1"> </div><div id="r2"> </div></div>
<div class="seg" id="seg">
<button data-f="trains">Trains</button>
<button data-f="clock">Clock</button>
<button data-f="spotify">Spotify</button>
<button data-f="text">Text</button>
</div>
<section class="card">
<div class="card-h"><h2>Custom Text</h2></div>
<form id="fText">
<label>Top row (max 17 chars)</label><input name="row1" maxlength="17" autocomplete="off" placeholder="HELLO">
<label>Bottom row (max 17 chars)</label><input name="row2" maxlength="17" autocomplete="off" placeholder="WORLD">
<div class="grid2">
<button class="save" type="submit">Display on board</button>
<button class="save" type="button" id="savePreset">Save as preset</button>
</div>
</form>
<div id="presets"></div>
</section>
<section class="card">
<div class="card-h"><h2>Train Settings</h2><span class="badge" id="bKey">API key missing</span></div>
<form id="fTrains">
<label>WMATA API key</label>
<input name="apikey" placeholder="Paste new key to update" autocomplete="off">
<p class="hint">Get a free key at <a href="https://developer.wmata.com" target="_blank" rel="noopener">developer.wmata.com</a>. Leave blank to keep the saved key.</p>
<div class="grid">
<div><label>Station code</label><input name="station" placeholder="A01"></div>
<div><label>Walk time (min)</label><input name="thresh" type="number" min="0"></div>
<div><label>Refresh (sec)</label><input name="refresh" type="number" min="5"></div>
</div>
<button class="save">Save train settings</button>
</form>
</section>
<section class="card">
<div class="card-h"><h2>Spotify</h2><span class="badge" id="bSp">Not connected</span></div>
<form id="fSpotify">
<label>Client ID</label><input name="id" autocomplete="off">
<label>Client secret</label><input name="secret" type="password" autocomplete="off">
<label>Refresh token</label><input name="token" type="password" autocomplete="off">
<p class="hint">Saved values are kept unless you enter new ones.</p>
<button class="save">Save Spotify settings</button>
</form>
</section>
<div class="toast" id="toast">Saved</div>
<script>
const $=id=>document.getElementById(id);
function toast(m){const t=$('toast');t.textContent=m;t.classList.add('show');clearTimeout(t._h);t._h=setTimeout(()=>t.classList.remove('show'),2200)}
document.querySelectorAll('#seg button').forEach(b=>b.onclick=()=>{
fetch('/feature',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+b.dataset.f})
.then(()=>{toast('Switched to '+b.textContent);poll()}).catch(()=>toast('Failed to switch'))});
function wire(id,url,msg){$(id).onsubmit=e=>{e.preventDefault();const f=e.target;
fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f))})
.then(r=>{if(!r.ok)throw 0;toast(msg);f.querySelectorAll('input[type=password],input[name=apikey],input[name=id]').forEach(i=>i.value='');loadConfig()})
.catch(()=>toast('Save failed'))}}
wire('fTrains','/trains','Train settings saved');
wire('fSpotify','/spotify','Spotify settings saved');
$('fText').onsubmit=e=>{e.preventDefault();const f=e.target;
fetch('/text',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(f))})
.then(r=>{if(!r.ok)throw 0;toast('Displaying on board');poll()}).catch(()=>toast('Failed'))};
$('savePreset').onclick=()=>{const f=$('fText');
if(!f.row1.value&&!f.row2.value){toast('Type something first');return}
fetch('/preset',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({action:'save',row1:f.row1.value,row2:f.row2.value})})
.then(r=>{if(!r.ok)throw 0;toast('Preset saved');loadPresets()}).catch(()=>toast('Preset slots full (max 6)'))};
function loadPresets(){fetch('/presets').then(r=>r.json()).then(a=>{
const d=$('presets');d.innerHTML='';
if(!a.length){d.innerHTML='<p class="presets-empty">No presets yet. Type text above and press "Save as preset".</p>';return}
a.forEach(p=>{
const el=document.createElement('div');el.className='preset';el.title='Click to display';
const tx=document.createElement('span');tx.className='tx';tx.textContent=(p.r1||' ')+'\n'+(p.r2||' ');
el.onclick=()=>{const f=$('fText');f.row1.value=p.r1;f.row2.value=p.r2;f.requestSubmit()};
const x=document.createElement('button');x.className='del';x.textContent='\u2715';x.title='Delete preset';
x.onclick=ev=>{ev.stopPropagation();
fetch('/preset',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({action:'delete',slot:p.slot})})
.then(()=>{toast('Preset deleted');loadPresets()}).catch(()=>toast('Delete failed'))};
el.append(tx,x);d.append(el)})}).catch(()=>{})}
function loadConfig(){fetch('/config').then(r=>r.json()).then(c=>{
const f=$('fTrains');f.station.value=c.station;f.thresh.value=c.thresh;f.refresh.value=c.refresh;
const t=$('fText');if(document.activeElement!==t.row1)t.row1.value=c.text1;if(document.activeElement!==t.row2)t.row2.value=c.text2;
$('bKey').textContent=c.apikey?'API key saved':'API key missing';$('bKey').classList.toggle('ok',c.apikey);
$('bSp').textContent=c.spotify?'Connected':'Not connected';$('bSp').classList.toggle('ok',c.spotify);
}).catch(()=>{})}
function poll(){fetch('/status').then(r=>r.json()).then(s=>{
$('r1').textContent=s.row1||' ';$('r2').textContent=s.row2||' ';
$('dot').classList.add('on');$('conn').textContent='Online';
document.querySelectorAll('#seg button').forEach(b=>b.classList.toggle('active',s.feature==b.dataset.f));
}).catch(()=>{$('dot').classList.remove('on');$('conn').textContent='Offline'})}
setInterval(poll,3000);poll();loadConfig();loadPresets();
</script></body></html>
)HTML";

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  return s;
}

void setupWebServer() {
  server.on("/", HTTP_GET,
            []() { server.send_P(200, "text/html", PORTAL_HTML); });
  server.on("/status", HTTP_GET, []() {
    String j = "{\"feature\":\"" + AppSettings.activeFeature +
               "\",\"row1\":\"" + jsonEscape(lastRow1) + "\",\"row2\":\"" +
               jsonEscape(lastRow2) + "\"}";
    server.send(200, "application/json", j);
  });
  server.on("/config", HTTP_GET, []() {
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
  });
  server.on("/text", HTTP_POST, []() {
    AppSettings.textRow1 = server.arg("row1");
    AppSettings.textRow2 = server.arg("row2");
    AppSettings.activeFeature = "text";
    saveSettings();
    featureGen++;
    server.send(200, "text/plain", "ok");
  });
  server.on("/presets", HTTP_GET, []() {
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
  });
  server.on("/preset", HTTP_POST, []() {
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
      // Shift remaining presets down so slots stay compact.
      for (int i = slot; i < MAX_TEXT_PRESETS - 1; i++) {
        TextPresets[i] = TextPresets[i + 1];
      }
      TextPresets[MAX_TEXT_PRESETS - 1] = "";
      saveSettings();
      server.send(200, "text/plain", "ok");
    } else {
      server.send(400, "text/plain", "unknown action");
    }
  });
  server.on("/feature", HTTP_POST, []() {
    String n = server.arg("name");
    if (n == "trains" || n == "clock" || n == "spotify" || n == "text") {
      AppSettings.activeFeature = n;
      saveSettings();
      featureGen++;
      server.send(200, "text/plain", "ok");
    } else {
      server.send(400, "text/plain", "unknown feature");
    }
  });
  server.on("/trains", HTTP_POST, []() {
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
  });
  server.on("/spotify", HTTP_POST, []() {
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
  });
  server.begin();
  MDNS.addService("http", "tcp", 80);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(PORTAL_BUTTON_PIN, INPUT_PULLUP);
  loadSettings();
  startConfigPortal(false);
  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
  ArduinoOTA.setHostname("SplitflapBoard");
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
