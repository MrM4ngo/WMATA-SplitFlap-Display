# WMATA Splitflap Display Board

ESP32 firmware for a **2-row × 17-column** split-flap display. The board pulls live WMATA Metro train predictions, shows the current time, and can switch to clock, Spotify, or custom text modes — all configurable from a web portal on your WiFi network.

## The Board

This project is built for a split-flap module with **2 rows** and **17 characters per row** (34 flaps total).

| Row | Content | Format | Example |
|-----|---------|--------|---------|
| Top | Current time (24-hour) | centered | `      14:51      ` |
| Bottom | Next train | left-aligned | `Shady Grv 3 RD    ` |

Trains are shown **one at a time** on the bottom row. Each train displays for `refresh interval ÷ number of trains` seconds before flipping to the next.

Board dimensions are defined in `firmware/include/Config.h` (`BOARD_ROWS`, `BOARD_COLS`).

## Features

- **Live WMATA predictions** — Fetches real-time arrival data from the [WMATA API](https://developer.wmata.com/)
- **Walk-time filtering** — Only shows trains you can realistically catch based on your travel time to the platform
- **2×17 split-flap layout** — 24-hour clock on top, train info on bottom, one train per cycle
- **Abbreviated station names** — Long WMATA names shortened to fit 17 characters
- **Web control portal** — Live board preview, feature switching, and settings from any browser on your network
- **Custom text mode** — Type messages for both rows and save up to 6 presets
- **Spotify now playing** — Song on top, artist on bottom (parenthetical text stripped from titles)
- **Clock mode** — Time on top, date on bottom
- **WiFi captive portal** — First-time setup for WiFi credentials and WMATA API key
- **OTA updates** — Flash new firmware over WiFi after the first USB upload
- **Night mode** — NTP-synced clock; stops polling outside Metro service hours and shows `METRO CLOSED`
- **Hardened networking** — Pinned WMATA root CA, automatic WiFi reconnect, watchdog restart after repeated failures

## Requirements

- ESP32 dev board
- 2×17 split-flap display module
- A free [WMATA developer API key](https://developer.wmata.com/)
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)

## Quick Start

1. Clone the repository:

   ```bash
   git clone https://github.com/MrM4ngo/WMATA-SplitFlap-Display.git
   cd WMATA-SplitFlap-Display/firmware
   ```

2. Adjust `upload_port` / `monitor_port` in `platformio.ini` for your COM port, then build and upload:

   ```bash
   pio run -t upload
   pio device monitor
   ```

3. On first boot the ESP32 opens WiFi access point **SplitflapBoard**. Connect and enter your WiFi password, WMATA API key, station code, minute threshold, and refresh interval.
4. Hold **BOOT** for 3 seconds anytime to reopen the config portal.

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

## Web Portal

Once the board is on your WiFi, open the control page from your phone or computer:

- **`http://SplitflapBoard.local`** (or the IP shown in the serial monitor)

The portal shows a live preview of both board rows and lets you switch the active **feature**:

| Feature | Top row | Bottom row |
|---------|---------|------------|
| **Trains** | Time (24-hour) | Next train, one per cycle |
| **Clock** | Time (24-hour) | Date, e.g. `MON JUL 06` |
| **Spotify** | Song name | Artist name |
| **Text** | Your custom text | Your custom text |

Feature choice and all settings are saved to flash and survive reboots. Train settings (station, threshold, refresh) can also be changed from the portal without reopening the captive portal.

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

## Configuration

All runtime settings are stored in ESP32 flash and configured through the web portal or captive portal on first boot:

| Setting | Description | Default |
|---------|-------------|---------|
| WMATA API key | Your developer API key | (none) |
| Station code | WMATA station code for your home stop | `A01` |
| Walk time (min) | Minimum minutes until arrival | `0` |
| Refresh (sec) | Seconds for one full cycle through all trains | `30` |

Station codes are listed in `wmata_stations.csv`.

No credentials are compiled into the firmware.

## Project Structure

```
WMATA-SplitFlap-Display/
├── wmata_stations.csv     # Full WMATA station reference
└── firmware/              # ESP32 PlatformIO project
    ├── platformio.ini
    ├── include/Config.h   # BOARD_ROWS/COLS, settings struct
    └── src/
        ├── main.cpp       # Display logic, web portal, WiFi, NTP, OTA
        ├── Settings.cpp   # NVS settings persistence
        └── Stations.cpp   # Station abbreviation map
```

## Reliability

- WiFi reconnects automatically with a 15-second timeout; 10 consecutive failures trigger a reboot.
- HTTPS verifies WMATA's certificate against a pinned root CA.
- NTP keeps the top-row clock accurate (Eastern time with DST).

## Abbreviated Station Names

`firmware/src/Stations.cpp` maps full WMATA station names to short labels. Station names longer than 10 characters is truncated/abbreviated to fit the board.

## License

This project is provided as-is for personal use.
