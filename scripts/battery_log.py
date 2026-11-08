#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Record the battery readings of a battery_log.conf build as CSV.

The power manager logs a reading every 5 s; this listens on the console.

  scripts/battery_log.py /dev/ttyACM0 charge.csv

Every line received also goes to <csv>.log, so warnings, resets and the
power-off are kept. Stop with Ctrl+C. Needs pyserial.
"""

import argparse
import csv
import datetime
import re
import sys

import serial

READING = re.compile(
    r"\[(?P<uptime>\d+:\d+:\d+\.\d+)[,\d]*\].*battery (?P<percent>\d+) %, (?P<mv>\d+) mV, (?P<ua>-?\d+) uA, "
    r"(?P<mc>-?\d+) mC, charge (?P<charge>\d), usb (?P<usb>\d)"
)
CHARGE = ["idle", "trickle", "constant current", "constant voltage", "complete"]
HIGHLIGHT = ("<err>", "<wrn>", "reset cause", "powering off", "ship mode", "fuel gauge")


def uptime_seconds(text: str) -> float:
    hours, minutes, seconds = text.split(":")
    return int(hours) * 3600 + int(minutes) * 60 + float(seconds)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="serial port, e.g. /dev/ttyUSB0")
    parser.add_argument("csv", help="output CSV (appended to if it exists)")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    with (
        serial.Serial(args.port, args.baud, timeout=1) as port,
        open(args.csv, "a", newline="") as csv_file,
        open(args.csv + ".log", "a") as raw_log,
    ):
        writer = csv.writer(csv_file)
        if csv_file.tell() == 0:
            writer.writerow(["time", "uptime_s", "percent", "mv", "ua", "die_c", "charge", "usb"])
        while True:
            line = port.readline().decode(errors="replace").strip()
            if not line:
                continue
            now = datetime.datetime.now().isoformat(timespec="seconds")
            raw_log.write(f"{now} {line}\n")
            raw_log.flush()
            reading = READING.search(line)
            if reading:
                row = [
                    now,
                    f"{uptime_seconds(reading['uptime']):.1f}",
                    reading["percent"],
                    reading["mv"],
                    reading["ua"],
                    f"{int(reading['mc']) / 1000:.1f}",
                    CHARGE[int(reading["charge"])],
                    reading["usb"],
                ]
                writer.writerow(row)
                csv_file.flush()
                print(" ".join(row[2:]))
            elif any(word in line for word in HIGHLIGHT):
                print(line)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
