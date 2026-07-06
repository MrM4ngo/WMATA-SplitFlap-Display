#pragma once

#include <Arduino.h>
#include <map>

// Runtime settings stored in NVS flash. Edited through the WiFi config
// portal (hold the BOOT button, or automatic on first boot) instead of
// being compiled in, so no secrets live in the source tree.
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

// Root CA that signs api.wmata.com's certificate
// (SSL.com TLS RSA Root CA 2022, expires 2046-08-19).
extern const char WMATA_ROOT_CA[];

// Full station name -> split-flap-friendly abbreviation.
extern const std::map<String, String> AbrvStations;
