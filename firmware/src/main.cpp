#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

#include "Config.h"


struct Train
{
  String Destination;
  int Min; // minutes until arrival
  String Line;
};

static const int MIN_NONE = -9999;

// Helper Functions

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

void PrintSpacer(const std::vector<Train> &upcomingTrains)
{
  size_t size = upcomingTrains.size();
  if (size == 0)
  {
    return;
  }
  unsigned long delayPerTrain = TrainRefreshTime / size;

  for (size_t i = 0; i < size; i++)
  {
    Serial.print(upcomingTrains[i].Destination);
    Serial.print("  ");
    Serial.print(upcomingTrains[i].Min);
    Serial.print("  ");
    Serial.println(upcomingTrains[i].Line);

    delay(delayPerTrain);
  }
}

// Main Functions

bool TrainPredictions(const String &StationCode, JsonDocument &doc)
{
  String url = "https://api.wmata.com/StationPrediction.svc/json/GetPrediction/" + StationCode;

  WiFiClientSecure client;
  client.setInsecure(); 

  HTTPClient http;
  http.begin(client, url);
  http.addHeader("api_key", API_KEY);

  int statusCode = http.GET();

  if (statusCode == 200)
  {
    String payload = http.getString();
    DeserializationError err = deserializeJson(doc, payload);
    http.end();
    if (err)
    {
      Serial.print("JSON parse error: ");
      Serial.println(err.c_str());
      return false;
    }
    return true;
  }
  else
  {
    Serial.print("Error: ");
    Serial.println(statusCode);
    http.end();
    return false;
  }
}

std::vector<Train> GetUpcomingTrains(const String &StationCode, int Threshold)
{
  std::vector<Train> upcomingTrains;

  JsonDocument data;
  bool ok = TrainPredictions(StationCode, data);

  if (!ok || !data["Trains"].is<JsonArray>())
  {
    Serial.println("No trains found or API error");
    return upcomingTrains;
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
      String destinationName = AbbreviateChecker(destination);

      Train tempTrain;
      tempTrain.Destination = destinationName;
      tempTrain.Min = minutes;
      tempTrain.Line = train["Line"].as<String>();

      upcomingTrains.push_back(tempTrain);
    }
  }

  return upcomingTrains;
}

// WiFi connect helper
void connectWiFi()
{
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
}


void setup()
{
  Serial.begin(115200);
  delay(1000);
  connectWiFi();
}

void loop()
{
  std::vector<Train> upcoming = GetUpcomingTrains(TrainStationCode, MinuteThreshold);
  PrintSpacer(upcoming);
}
