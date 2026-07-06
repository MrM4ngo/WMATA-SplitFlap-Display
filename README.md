# WMATA Splitflap Display Board

A Python service that pulls live WMATA Metro train predictions and formats them for a split-flap display board. Shows upcoming arrivals at your station, filtered by how long it takes you to walk there, with abbreviated destination names that fit limited character displays.

## Features

- **Live WMATA predictions** — Fetches real-time arrival data from the [WMATA API](https://developer.wmata.com/)
- **Walk-time filtering** — Only shows trains you can realistically catch based on your travel time to the platform
- **Split-flap friendly output** — Abbreviated station names for displays with a 10-character limit
- **Configurable polling** — Set your station, refresh interval, and minute threshold in one place
- **Service alerts** — Shows `RD LINE DELAY`-style alerts from the WMATA Incidents API for lines serving your station
- **Desktop simulator** — Renders the split-flap board (with flap animation) in your terminal, no hardware needed
- **Web config portal** — WiFi credentials, API key, and station settings are entered via a captive portal and stored in flash; nothing is compiled in
- **OTA updates** — Flash new firmware over WiFi after the first USB upload
- **Night mode** — Syncs time via NTP and stops polling outside Metro service hours
- **Hardened networking** — Pinned WMATA root CA, automatic WiFi reconnect, and a watchdog restart after repeated failures

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

## Desktop Simulator

`simulator.py` renders the split-flap board in your terminal — including the character-by-character flap animation — so you can test display formatting without flashing the ESP32.

```bash
python simulator.py           # live data (uses .env and Variables.py)
python simulator.py --demo    # canned data, no API key needed
```

Options: `--rows N` sets the number of board rows (default 3), `--refresh S` overrides the refresh interval. Alerts and trains that don't fit on the board are cycled through in pages, like the real display.

## Project Structure

```
TrainAPISplitFlap/
├── Trains.py              # WMATA API client and train filtering logic
├── Variables.py           # Station, threshold, and refresh settings
├── simulator.py           # Terminal split-flap simulator
├── wmata_stations.csv     # Full WMATA station reference
├── requirements.txt       # Python dependencies
├── .env                   # API key (not committed — create locally)
└── firmware/              # ESP32 PlatformIO firmware
    ├── platformio.ini
    ├── include/Config.h   # Settings struct + declarations
    └── src/
        ├── main.cpp       # Train logic, WiFi, NTP, OTA, incidents
        ├── Settings.cpp   # NVS-backed settings + pinned root CA
        └── Stations.cpp   # Station abbreviation map
```

## ESP32 Firmware (PlatformIO)

The `firmware/` folder contains a C++ port of `Trains.py` for ESP32 boards. It fetches WMATA predictions over WiFi and prints upcoming trains to the serial monitor.

No credentials are compiled in — everything is configured through a web portal on first boot:

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI).
2. Open the `firmware/` directory as your PlatformIO project.
3. Adjust `upload_port` / `monitor_port` in `firmware/platformio.ini` for your COM port, then build and upload:

   ```bash
   cd firmware
   pio run -t upload
   pio device monitor
   ```

4. On first boot the ESP32 opens a WiFi access point named **WMATA-Setup**. Connect to it from your phone or laptop and a captive portal appears (browse to `192.168.4.1` if it doesn't).
5. Pick your WiFi network and enter its password, your WMATA API key, station code, minute threshold, and refresh interval. Everything is saved to flash (NVS) and survives reboots and reflashing.

To change settings later (new WiFi, different station), hold the **BOOT** button for 3 seconds — the WMATA-Setup portal reopens.

### OTA updates

After the first USB flash, the device accepts over-the-air updates as `wmata-splitflap.local`. Uncomment the two OTA lines at the bottom of `platformio.ini` and `pio run -t upload` flashes over WiFi — no cable needed.

### Reliability behavior

- WiFi drops are detected each cycle and reconnected with a 15-second timeout; after 10 consecutive failed cycles the board restarts itself.
- Time is synced via NTP (Eastern time with DST). Outside Metro service hours (Mon–Thu 5am–12am, Fri 5am–1am, Sat 7am–1am, Sun 7am–12am) the board prints `METRO CLOSED` and stops calling the API to save your daily quota.
- HTTPS calls verify WMATA's certificate against its pinned root CA (SSL.com TLS RSA Root CA 2022, valid to 2046) instead of skipping validation.
- Line incidents are fetched every 5 minutes; alerts affecting your station's lines are shown as e.g. `RD LINE DELAY` before the arrival list.

## Abbreviated Station Names

`Variables.py` includes an `AbrvStations` dictionary that maps full WMATA station names to short labels for split-flap displays. Names longer than 10 characters (including spaces) are abbreviated automatically.

## License

This project is provided as-is for personal use.
