# TrainAPISplitFlap

A Python service that pulls live WMATA Metro train predictions and formats them for a split-flap display board. Shows upcoming arrivals at your station, filtered by how long it takes you to walk there, with abbreviated destination names that fit limited character displays.

## Features

- **Live WMATA predictions** — Fetches real-time arrival data from the [WMATA API](https://developer.wmata.com/)
- **Walk-time filtering** — Only shows trains you can realistically catch based on your travel time to the platform
- **Split-flap friendly output** — Abbreviated station names for displays with a 10-character limit
- **Configurable polling** — Set your station, refresh interval, and minute threshold in one place

## Requirements

- Python 3.8+
- A free [WMATA developer API key](https://developer.wmata.com/)

## Installation

1. Clone the repository:

   ```bash
   git clone https://github.com/MrM4ngo/TrainAPISplitFlap.git
   cd TrainAPISplitFlap
   ```

2. Install dependencies:

   ```bash
   pip install -r requirements.txt
   ```

3. Create a `.env` file in the project root:

   ```env
   API_KEY=your_wmata_api_key_here
   ```

## Configuration

Edit `Variables.py` to match your setup:

| Variable | Description | Default |
|----------|-------------|---------|
| `TrainStationCode` | WMATA station code for your home stop | `D02` |
| `MinuteThreshold` | Minimum minutes until arrival (your walk time to the platform) | `0` |
| `TrainRefreshTime` | How often to poll the API, in seconds | `30` |

Station codes are listed in `wmata_stations.csv`. Find your stop and use its `Code` column value.

## Usage

Run the polling loop:

```bash
python Trains.py
```

Each cycle prints a list of upcoming trains:

```python
[
  {"Destination": "Shady Grove", "Min": 3, "Line": "RD"},
  {"Destination": "Glenmont", "Min": 7, "Line": "RD"}
]
```

- **Min** — Minutes until arrival (`0` means boarding or arriving now)
- **Line** — Metro line code (e.g. `RD`, `BL`, `OR`, `SV`, `GR`, `YL`)

You can also import `Trains()` in your own code to feed a split-flap controller or other display hardware.

## Project Structure

```
TrainAPISplitFlap/
├── Trains.py           # WMATA API client and train filtering logic
├── Variables.py        # Station, threshold, and refresh settings
├── wmata_stations.csv  # Full WMATA station reference
├── requirements.txt    # Python dependencies
└── .env                # API key (not committed — create locally)
```

## Abbreviated Station Names

`Variables.py` includes an `AbrvStations` dictionary that maps full WMATA station names to short labels for split-flap displays. Names longer than 10 characters (including spaces) are abbreviated automatically.

## License

This project is provided as-is for personal use.
