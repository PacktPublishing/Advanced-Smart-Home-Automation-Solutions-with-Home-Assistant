#!/usr/bin/env python3

import csv
import sys
from datetime import datetime
from pathlib import Path

OUTPUT_FILE = Path("/config/www/dishwasher_hourly_energy.csv")

energy_kwh = float(sys.argv[1])
now = datetime.now()

row = {
    "timestamp": now.isoformat(timespec="seconds"),
    "hour": now.hour,
    "weekday": now.weekday(),
    "is_weekend": int(now.weekday() >= 5),
    "energy_kwh": round(energy_kwh, 4),
}

file_exists = OUTPUT_FILE.exists()

with OUTPUT_FILE.open("a", newline="", encoding="utf-8") as file:
    writer = csv.DictWriter(file, fieldnames=row.keys())

    if not file_exists:
        writer.writeheader()

    writer.writerow(row)