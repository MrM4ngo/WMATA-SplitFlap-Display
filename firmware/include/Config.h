#pragma once

#include <Arduino.h>
#include <map>

static const int MAX_TEXT_PRESETS = 6;

struct Settings {
  String apiKey;
  String stationCode;
  int minuteThreshold;
  unsigned long refreshSeconds;
  String activeFeature;
  String spotifyId;
  String spotifySecret;
  String spotifyRefresh;
  String textRow1;
  String textRow2;
};

extern Settings AppSettings;

// Each preset stores "row1\nrow2"; empty string = unused slot.
extern String TextPresets[MAX_TEXT_PRESETS];

void loadSettings();
void saveSettings();

extern const char WMATA_ROOT_CA[];

static const int BOARD_ROWS = 2;
static const int BOARD_COLS = 17;

extern const std::map<String, String> AbrvStations;
