#pragma once

#include <Arduino.h>
#include <map>

struct Settings
{
  String apiKey;
  String stationCode;
  int minuteThreshold;
  unsigned long refreshSeconds;
};

extern Settings AppSettings;

void loadSettings();
void saveSettings();

extern const char WMATA_ROOT_CA[];

static const int BOARD_ROWS = 2;
static const int BOARD_COLS = 17;

extern const std::map<String, String> AbrvStations;
