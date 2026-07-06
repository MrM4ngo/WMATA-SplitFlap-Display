import os
import requests
from dotenv import load_dotenv
import Variables
import time

load_dotenv()
api_key = os.getenv("API_KEY")

# Helper Functions


def MinToInt(MinValue):
    if MinValue in ("ARR", "BRD"):
        return 0
    elif MinValue in ("---", "", None):
        return None
    else:
        return int(MinValue)


def AbbreviateChecker(name):
    if name in Variables.AbrvStations:
        return Variables.AbrvStations[name]
    else:
        return name


def PrintSpacer(UpcomingTrains):
    size = len(UpcomingTrains)
    for i in range(size):
        print(
            UpcomingTrains[i]["Destination"],
            UpcomingTrains[i]["Min"],
            UpcomingTrains[i]["Line"],
        )
        time.sleep(Variables.TrainRefreshTime / size)


# Main Functions


def TrainPredictions(StationCode):
    url = (
        f"https://api.wmata.com/StationPrediction.svc/json/GetPrediction/{StationCode}"
    )
    headers = {"api_key": api_key}

    response = requests.get(url, headers=headers)
    if response.status_code == 200:
        return response.json()
    else:
        print(f"Error: {response.status_code}")
        return None


def UpcomingTrains(StationCode, Threshold):

    UpcomingTrains = []

    data = TrainPredictions(StationCode)
    # print(data)

    if data is None or "Trains" not in data:
        print("No trains found or API error")
        return UpcomingTrains

    for train in data["Trains"]:
        if train["Destination"] not in [
            SavedTrain["Destination"] for SavedTrain in UpcomingTrains
        ]:
            minutes = MinToInt(train["Min"])
            if minutes is not None and minutes >= Threshold:
                abbrevname = AbbreviateChecker(train["Destination"])
                DestinationName = (
                    abbrevname[0] if isinstance(abbrevname, list) else abbrevname
                )
                TempTrain = {
                    "Destination": DestinationName,
                    "Min": minutes,
                    "Line": train["Line"],
                }
                UpcomingTrains.append(TempTrain)

    return UpcomingTrains


if __name__ == "__main__":
    while True:
        PrintSpacer(
            UpcomingTrains(Variables.TrainStationCode, Variables.MinuteThreshold)
        )
