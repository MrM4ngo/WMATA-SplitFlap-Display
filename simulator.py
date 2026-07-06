"""Desktop split-flap simulator.

Renders WMATA train predictions the way the physical split-flap board
would show them, including the character-by-character flap animation,
so display formatting can be tested without flashing the ESP32.

Usage:
    python simulator.py           # live data (needs API_KEY in .env)
    python simulator.py --demo    # canned data, no API key required
"""

import argparse
import os
import sys
import time

# Character drum on a real split-flap module. Each flap steps through
# this sequence until it reaches the target character.
FLAP_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-'/.:&"

DEST_WIDTH = 10
MIN_WIDTH = 3
LINE_WIDTH = 2
ROW_WIDTH = DEST_WIDTH + 1 + MIN_WIDTH + 1 + LINE_WIDTH

FRAME_SECONDS = 0.04

DEMO_TRAINS = [
    {"Destination": "Shady Grv", "Min": 3, "Line": "RD"},
    {"Destination": "Glenmont", "Min": 7, "Line": "RD"},
    {"Destination": "Frnc-Sprngfld", "Min": 12, "Line": "BL"},
]

DEMO_ALERTS = ["RD LINE DELAY"]


def flap_index(ch):
    """Position of ch on the drum; unknown characters render as blank."""
    ch = ch.upper()
    idx = FLAP_CHARS.find(ch)
    return idx if idx >= 0 else 0


def format_row(train):
    dest = str(train["Destination"])[:DEST_WIDTH]
    mins = str(train["Min"])[:MIN_WIDTH]
    line = str(train["Line"])[:LINE_WIDTH]
    return f"{dest:<{DEST_WIDTH}} {mins:>{MIN_WIDTH}} {line:<{LINE_WIDTH}}"


def format_message_row(message):
    return f"{message[:ROW_WIDTH]:<{ROW_WIDTH}}"


class Board:
    def __init__(self, rows):
        self.rows = rows
        self.current = [[0] * ROW_WIDTH for _ in range(rows)]

    def render(self):
        sys.stdout.write("\x1b[H")
        top = "+" + "-" * (ROW_WIDTH + 2) + "+"
        print(top)
        for row in self.current:
            text = "".join(FLAP_CHARS[i] for i in row)
            print(f"| {text} |")
        print(top)
        sys.stdout.flush()

    def flip_to(self, lines):
        """Animate every flap stepping through the drum to the new text."""
        target = []
        for r in range(self.rows):
            text = lines[r] if r < len(lines) else ""
            text = f"{text:<{ROW_WIDTH}}"[:ROW_WIDTH]
            target.append([flap_index(c) for c in text])

        drum = len(FLAP_CHARS)
        settled = False
        while not settled:
            settled = True
            for r in range(self.rows):
                for c in range(ROW_WIDTH):
                    if self.current[r][c] != target[r][c]:
                        self.current[r][c] = (self.current[r][c] + 1) % drum
                        settled = False
            self.render()
            time.sleep(FRAME_SECONDS)


def fetch_live():
    import Trains
    import Variables

    trains = Trains.UpcomingTrains(Variables.TrainStationCode, Variables.MinuteThreshold)
    return trains, []


def build_pages(trains, alerts, rows):
    """Split content into board-sized pages, alerts first."""
    lines = [format_message_row(a) for a in alerts]
    lines += [format_row(t) for t in trains]
    if not lines:
        lines = [format_message_row("NO TRAINS")]

    return [lines[i:i + rows] for i in range(0, len(lines), rows)]


def main():
    parser = argparse.ArgumentParser(description="Split-flap display simulator")
    parser.add_argument("--demo", action="store_true", help="use canned data instead of the WMATA API")
    parser.add_argument("--rows", type=int, default=3, help="number of display rows (default 3)")
    parser.add_argument("--refresh", type=int, default=None, help="seconds between data refreshes")
    args = parser.parse_args()

    if args.demo:
        refresh = args.refresh or 15
    else:
        import Variables
        refresh = args.refresh or Variables.TrainRefreshTime

    os.system("")  # enable ANSI escape codes in the Windows console
    sys.stdout.write("\x1b[2J\x1b[?25l")  # clear screen, hide cursor

    board = Board(args.rows)
    try:
        while True:
            if args.demo:
                trains, alerts = DEMO_TRAINS, DEMO_ALERTS
            else:
                trains, alerts = fetch_live()

            pages = build_pages(trains, alerts, args.rows)
            per_page = max(refresh / len(pages), 3)

            for page in pages:
                board.flip_to(page)
                time.sleep(per_page)
    except KeyboardInterrupt:
        pass
    finally:
        sys.stdout.write("\x1b[?25h\n")  # restore cursor
        sys.stdout.flush()


if __name__ == "__main__":
    main()
