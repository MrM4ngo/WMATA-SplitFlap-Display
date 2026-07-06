#pragma once

#include <Arduino.h>
#include <map>

// ---------------- WiFi credentials ----------------
extern const char *WIFI_SSID;
extern const char *WIFI_PASSWORD;

// ---------------- WMATA API ----------------
// Equivalent of the .env API_KEY loaded via dotenv in the Python version.
extern const char *API_KEY;

// ---------------- Station settings ----------------
// Equivalent of Variables.TrainStationCode / Variables.MinuteThreshold
extern const char *TrainStationCode;
extern const int MinuteThreshold;

// Equivalent of Variables.TrainRefreshTime (was in seconds/whatever unit you
// used with time.sleep(); here it's expressed in milliseconds).
extern const unsigned long TrainRefreshTime;

// Equivalent of Variables.AbrvStations. The Python dict values could either
// be a plain string or a list of strings (you used
// `abbrevname[0] if isinstance(abbrevname, list) else abbrevname`).
// To keep the ESP32 side simple, this map is String -> String; if you had
// multiple abbreviations for one destination in Python, just pick the first
// one when you fill this map in Config.cpp.
extern std::map<String, String> AbrvStations;
