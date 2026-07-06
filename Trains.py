import os
import requests
from dotenv import load_dotenv
import Variables
import time
from datetime import datetime

load_dotenv()
api_key = os.getenv("API_KEY")


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


def format_time_line(width=None):
    width = width or Variables.DisplayCols
    text = datetime.now().strftime("%H:%M")
    return f"{text[:width]:^{width}}"


def format_train_line(train, width=None):
    width = width or Variables.DisplayCols
    text = f"{train['Destination']} {train['Min']} {train['Line']}"
    return f"{text[:width]:<{width}}"


def format_display(train, width=None):
    return [format_time_line(width), format_train_line(train, width)]


def PrintSpacer(UpcomingTrains):
    size = len(UpcomingTrains)
    width = Variables.DisplayCols
    if size == 0:
        lines = format_display({"Destination": "NO TRAINS", "Min": "", "Line": ""}, width)
        print(lines[0])
        print(lines[1])
        time.sleep(Variables.TrainRefreshTime)
        return
    delay = Variables.TrainRefreshTime / size
    for train in UpcomingTrains:
        lines = format_display(train, width)
        print(lines[0])
        print(lines[1])
        time.sleep(delay)


def TrainPredictions(StationCode):
    url = f"https://api.wmata.com/StationPrediction.svc/json/GetPrediction/{StationCode}"
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
