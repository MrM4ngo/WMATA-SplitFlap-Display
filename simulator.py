import argparse
import os
import sys
import time

import Trains
import Variables

FLAP_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-'/.:&"
FRAME_SECONDS = 0.04


def flap_index(ch):
    ch = ch.upper()
    idx = FLAP_CHARS.find(ch)
    return idx if idx >= 0 else 0


class Board:
    def __init__(self, rows, width):
        self.rows = rows
        self.width = width
        self.current = [[0] * width for _ in range(rows)]

    def render(self):
        sys.stdout.write("\x1b[H")
        top = "+" + "-" * (self.width + 2) + "+"
        print(top)
        for row in self.current:
            text = "".join(FLAP_CHARS[i] for i in row)
            print(f"| {text} |")
        print(top)
        sys.stdout.flush()

    def flip_to(self, lines):
        target = []
        for r in range(self.rows):
            text = lines[r] if r < len(lines) else ""
            text = f"{text:<{self.width}}"[: self.width]
            target.append([flap_index(c) for c in text])
        drum = len(FLAP_CHARS)
        settled = False
        while not settled:
            settled = True
            for r in range(self.rows):
                for c in range(self.width):
                    if self.current[r][c] != target[r][c]:
                        self.current[r][c] = (self.current[r][c] + 1) % drum
                        settled = False
            self.render()
            time.sleep(FRAME_SECONDS)


def run_cycle(board, trains, refresh_seconds):
    size = len(trains)
    width = board.width
    if size == 0:
        board.flip_to(Trains.format_display({"Destination": "NO TRAINS", "Min": "", "Line": ""}, width))
        time.sleep(refresh_seconds)
        return
    delay = refresh_seconds / size
    for train in trains:
        board.flip_to(Trains.format_display(train, width))
        time.sleep(delay)


def main():
    parser = argparse.ArgumentParser(description="Split-flap display simulator")
    parser.add_argument("--rows", type=int, default=Variables.DisplayRows)
    parser.add_argument("--width", type=int, default=Variables.DisplayCols)
    parser.add_argument("--refresh", type=int, default=None)
    args = parser.parse_args()
    refresh = args.refresh or Variables.TrainRefreshTime
    os.system("")
    sys.stdout.write("\x1b[2J\x1b[?25l")
    board = Board(args.rows, args.width)
    try:
        while True:
            trains = Trains.UpcomingTrains(Variables.TrainStationCode, Variables.MinuteThreshold)
            run_cycle(board, trains, refresh)
    except KeyboardInterrupt:
        pass
    finally:
        sys.stdout.write("\x1b[?25h\n")
        sys.stdout.flush()


if __name__ == "__main__":
    main()
