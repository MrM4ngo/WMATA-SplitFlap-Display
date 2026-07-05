import os
import requests
import csv
from dotenv import load_dotenv
import Variables
import time

load_dotenv()
api_key = os.getenv("API_KEY")


def Trains(station_code,Threshold):

    def get_train_predictions(station_code):
        url = f"https://api.wmata.com/StationPrediction.svc/json/GetPrediction/{station_code}"
        headers = {"api_key": api_key}
        
        response = requests.get(url, headers=headers)
        if response.status_code == 200:
            return response.json()
        else:
            print(f"Error: {response.status_code}")
            return None
        
    def get_next_viable_trains(prediction_data, Threshold):
        trains = prediction_data.get("Trains", [])
        if not trains:
            return {}

        def min_to_int(min_value):
            if min_value in ("ARR", "BRD"):
                return 0
            elif min_value == "---":
                return None
            else:
                return int(min_value)

        Size = len(trains)
        # print(trains)
        # print(Size)

        TrainList = []

        for i in range(Size):
            if trains[i]["Destination"] not in [train["Destination"] for train in TrainList]:
                minutes = min_to_int(trains[i]["Min"])
                if minutes is not None and minutes >= Threshold:
                    TempTrain = {"Destination": trains[i]["Destination"], "Min": minutes, "Line": trains[i]["Line"]}
                    TrainList.append(TempTrain)
        
        return TrainList
    
    predictions = get_train_predictions(station_code)
    if predictions:
        next_trains = get_next_viable_trains(predictions, Threshold=Threshold)
    return next_trains
    

if __name__ == "__main__":
    while True:
        print(Trains(Variables.TrainStationCode, Variables.MinuteThreshold))
        time.sleep(Variables.TrainRefreshTime)