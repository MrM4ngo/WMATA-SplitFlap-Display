# WMATA Splitflap Display Board

A Python service and ESP32 firmware that pull live WMATA Metro train predictions and drive a **2-row × 17-column** split-flap display. The top row shows the current time; the bottom row cycles through upcoming arrivals at your station, filtered by walk time, with abbreviated destination names that fit the board width.

## The Board

This project is built for a split-flap module with **2 rows** and **17 characters per row** (34 flaps total).

| Row | Content | Format | Example |
|-----|---------|--------|---------|
| Top | Current time (24-hour) | centered | `      14:51      ` |
| Bottom | Next train | left-aligned | `Shady Grv 3 RD    ` |

Trains are shown **one at a time** on the bottom row. Each train displays for `TrainRefreshTime ÷ number of trains` seconds before flipping to the next — the same timing in Python, the simulator, and the ESP32 firmware.

Board dimensions are defined in `Variables.py` (`DisplayRows = 2`, `DisplayCols = 17`) and `firmware/include/Config.h` (`BOARD_ROWS`, `BOARD_COLS`).

## Features

- **Live WMATA predictions** — Fetches real-time arrival data from the [WMATA API](https://developer.wmata.com/)
- **Walk-time filtering** — Only shows trains you can realistically catch based on your travel time to the platform
- **2×17 split-flap layout** — 24-hour clock on top, train info on bottom, one train per cycle
- **Abbreviated station names** — Long WMATA names shortened to fit 17 characters
- **Desktop simulator** — Terminal preview with flap animation, same logic as the hardware
- **Web config portal** — WiFi credentials, API key, and station settings via captive portal, stored in flash
- **OTA updates** — Flash new firmware over WiFi after the first USB upload
- **Night mode** — NTP-synced clock; stops polling outside Metro service hours and shows `METRO CLOSED`
- **Hardened networking** — Pinned WMATA root CA, automatic WiFi reconnect, watchdog restart after repeated failures

## Requirements

- Python 3.8+
- A free [WMATA developer API key](https://developer.wmata.com/)
- ESP32 dev board (for hardware deployment)
- 2×17 split-flap display module

## Installation

1. Clone the repository:

   ```bash
   git clone https://github.com/MrM4ngo/WMATA-SplitFlap-Display.git
   cd WMATA-SplitFlap-Display
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
| `TrainStationCode` | WMATA station code for your home stop | `A02` |
| `MinuteThreshold` | Minimum minutes until arrival (walk time to platform) | `10` |
| `TrainRefreshTime` | Seconds for one full cycle through all trains | `30` |
| `DisplayRows` | Number of split-flap rows | `2` |
| `DisplayCols` | Characters per row | `17` |

Station codes are listed in `wmata_stations.csv`.

## Usage

### Python (serial output)

```bash
python Trains.py
```

Prints two 17-character lines per train:

```text
      14:51
Shady Grv 3 RD
      14:51
Glenmont 7 RD
```

### Desktop simulator

```bash
python simulator.py
```

Same data and timing as `Trains.py`, rendered with flap animation in the terminal. Press **Ctrl+C** to quit.

Optional flags (defaults come from `Variables.py`):

| Option | Description |
|--------|-------------|
| `--width N` | Characters per row |
| `--rows N` | Number of rows |
| `--refresh S` | Seconds per full train cycle |

## Project Structure

```
WMATA-SplitFlap-Display/
├── Trains.py              # WMATA API, display formatting, PrintSpacer
├── Variables.py           # Station, threshold, board size, abbreviations
├── simulator.py           # Terminal split-flap simulator
├── wmata_stations.csv     # Full WMATA station reference
├── requirements.txt       # Python dependencies
├── .env                   # API key (not committed)
└── firmware/              # ESP32 PlatformIO firmware
    ├── platformio.ini
    ├── include/Config.h   # BOARD_ROWS/COLS, settings struct
    └── src/
        ├── main.cpp       # Display logic, WiFi, NTP, OTA
        ├── Settings.cpp   # NVS settings + pinned root CA
        └── Stations.cpp   # Station abbreviation map
```

## ESP32 Firmware (PlatformIO)

The firmware mirrors `Trains.py` display logic: top row is the NTP-synced 24-hour clock, bottom row cycles through trains one at a time. Output goes to the serial monitor (wire this to your flap controller when ready).

No credentials are compiled in — configure through the web portal on first boot:

1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI).
2. Open the `firmware/` directory as your PlatformIO project.
3. Adjust `upload_port` / `monitor_port` in `firmware/platformio.ini`, then build and upload:

   ```bash
   cd firmware
   pio run -t upload
   pio device monitor
   ```

4. On first boot the ESP32 opens WiFi access point **WMATA-Setup**. Connect and enter your WiFi password, WMATA API key, station code, minute threshold, and refresh interval.
5. Hold **BOOT** for 3 seconds anytime to reopen the config portal.

Serial output example:

```text
      14:51
Shady Grv 3 RD
      14:52
Glenmont 7 RD
```

Outside Metro hours the board shows:

```text
      02:15
METRO CLOSED
```

## Web Portal (Features)

Once the board is on your WiFi, it serves a control page you can open anytime from your phone or computer:

- **`http://wmata-splitflap.local`** (or the IP shown in the serial monitor, e.g. `http://192.168.1.48`)

The portal shows a live preview of both board rows and lets you switch the active **feature**:

| Feature | Top row | Bottom row |
|---------|---------|------------|
| **Trains** | Time (24-hour) | Next train, one per cycle |
| **Clock** | Time (24-hour) | Date, e.g. `MON JUL 06` |
| **Spotify** | Time (24-hour) | Current song — artist (scrolls if long) |

Feature choice and all settings are saved to flash and survive reboots. Train settings (station, threshold, refresh) can also be changed from the portal without touching the WMATA-Setup captive portal.

### Spotify setup

The Spotify feature needs a one-time authorization so the board can read your currently playing track:

1. Create an app at [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard). Note the **Client ID** and **Client Secret**, and add `http://127.0.0.1:8888/callback` as a Redirect URI.
2. Authorize your account in a browser (replace `CLIENT_ID`):

   ```text
   https://accounts.spotify.com/authorize?client_id=CLIENT_ID&response_type=code&redirect_uri=http://127.0.0.1:8888/callback&scope=user-read-currently-playing
   ```

3. After approving, the browser redirects to a URL containing `?code=...`. Copy that code (it expires quickly).
4. Exchange it for a **refresh token** (replace the placeholders):

   ```bash
   curl -X POST https://accounts.spotify.com/api/token -H "Content-Type: application/x-www-form-urlencoded" -d "grant_type=authorization_code&code=CODE&redirect_uri=http://127.0.0.1:8888/callback&client_id=CLIENT_ID&client_secret=CLIENT_SECRET"
   ```

5. Paste the Client ID, Client Secret, and `refresh_token` from the response into the portal's **Spotify settings** form.

The refresh token does not expire; the board renews its access token automatically.

### OTA updates

After the first USB flash, uncomment the OTA lines in `platformio.ini` and upload over WiFi to `wmata-splitflap.local`.

### Reliability

- WiFi reconnects automatically with a 15-second timeout; 10 consecutive failures trigger a reboot.
- HTTPS verifies WMATA's certificate against a pinned root CA.
- NTP keeps the top-row clock accurate (Eastern time with DST).

## Abbreviated Station Names

`Variables.py` and `firmware/src/Stations.cpp` map full WMATA station names to short labels. Text longer than 17 characters is truncated to fit the board.

## License

This project is provided as-is for personal use.
