#!/usr/bin/env python3

import csv
import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Optional


# ---------------------------------------------------------
# File configuration
# ---------------------------------------------------------

CSV_FILE = Path(
    "/config/www/dishwasher_hourly_energy.csv"
)


# ---------------------------------------------------------
# Electricity-price configuration
# ---------------------------------------------------------
#
# These values are stored directly in the script.
# Therefore, no Home Assistant tariff or rate sensors
# are required.
#
# Adjust these example values to match your electricity
# plan.
# ---------------------------------------------------------

HIGH_RATE_START_HOUR = 15
HIGH_RATE_END_HOUR = 19

HIGH_RATE_USD_PER_KWH = 0.24
LOW_RATE_USD_PER_KWH = 0.11

# Ignore extremely small values that may be caused by
# sensor noise or rounding.
MINIMUM_ENERGY_KWH = 0.001


def print_json(data: dict[str, Any]) -> None:
    """Print a dictionary as valid JSON."""

    print(
        json.dumps(
            data,
            ensure_ascii=False,
        )
    )


def parse_integer(
    value: Any,
    default: int,
) -> int:
    """Safely convert a value to an integer."""

    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def read_latest_usage() -> Optional[dict[str, Any]]:
    """
    Read the CSV file and return the most recent row
    containing meaningful dishwasher energy consumption.
    """

    if not CSV_FILE.exists():
        return None

    latest_usage: Optional[dict[str, Any]] = None

    with CSV_FILE.open(
        mode="r",
        newline="",
        encoding="utf-8",
    ) as csv_file:
        reader = csv.DictReader(csv_file)

        for row in reader:
            try:
                timestamp = datetime.fromisoformat(
                    row["timestamp"]
                )

                energy_kwh = float(
                    row["energy_kwh"]
                )

                usage_hour = parse_integer(
                    row.get("hour"),
                    timestamp.hour,
                )

            except (
                KeyError,
                TypeError,
                ValueError,
            ):
                # Ignore incomplete or invalid CSV rows.
                continue

            if energy_kwh < MINIMUM_ENERGY_KWH:
                continue

            # Because the CSV is written chronologically,
            # the last valid non-zero row becomes the most
            # recent usage record.
            latest_usage = {
                "timestamp": timestamp,
                "hour": usage_hour,
                "energy_kwh": energy_kwh,
            }

    return latest_usage


def main() -> int:
    """Analyze the latest recorded dishwasher usage."""

    latest_usage = read_latest_usage()

    if latest_usage is None:
        print_json(
            {
                "state": "no_data",
                "generated_at": datetime.now().isoformat(
                    timespec="seconds"
                ),
                "reason": (
                    "No meaningful dishwasher energy "
                    "consumption was found in the CSV file."
                ),
            }
        )
        return 0

    usage_timestamp = latest_usage["timestamp"]
    usage_hour = latest_usage["hour"]
    energy_kwh = latest_usage["energy_kwh"]

    used_during_high_rate = (
        HIGH_RATE_START_HOUR
        <= usage_hour
        < HIGH_RATE_END_HOUR
    )

    if used_during_high_rate:
        usage_period = "higher_rate"
        current_rate = HIGH_RATE_USD_PER_KWH
    else:
        usage_period = "lower_rate"
        current_rate = LOW_RATE_USD_PER_KWH

    current_cost = (
        energy_kwh
        * current_rate
    )

    lower_rate_cost = (
        energy_kwh
        * LOW_RATE_USD_PER_KWH
    )

    estimated_savings = max(
        current_cost - lower_rate_cost,
        0.0,
    )

    if current_cost > 0:
        estimated_savings_percent = (
            estimated_savings
            / current_cost
            * 100
        )
    else:
        estimated_savings_percent = 0.0

    recommendation_required = (
        used_during_high_rate
        and estimated_savings >= 0.01
    )

    if recommendation_required:
        state = "recommended"

        recommendation = (
            "Delay the dishwasher until the lower-rate "
            "period begins."
        )

        recommended_start_time = (
            f"{HIGH_RATE_END_HOUR:02d}:00"
        )

        reason = (
            "The most recent dishwasher usage occurred "
            "during the higher electricity-rate period. "
            f"Starting it at "
            f"{HIGH_RATE_END_HOUR:02d}:00 or later could "
            f"save approximately "
            f"${estimated_savings:.2f} for a similar cycle."
        )

    else:
        state = "not_recommended"

        recommendation = (
            "Keep the current dishwasher schedule."
        )

        recommended_start_time = "keep_current_time"

        reason = (
            "The most recent dishwasher usage occurred "
            "during the lower electricity-rate period, "
            "so shifting the cycle would not provide an "
            "estimated cost benefit."
        )

    result = {
        "state": state,
        "generated_at": datetime.now().isoformat(
            timespec="seconds"
        ),
        "usage_timestamp": usage_timestamp.isoformat(
            timespec="seconds"
        ),
        "usage_hour": usage_hour,
        "energy_kwh": round(
            energy_kwh,
            4,
        ),
        "usage_period": usage_period,
        "current_rate": round(
            current_rate,
            4,
        ),
        "lower_rate": round(
            LOW_RATE_USD_PER_KWH,
            4,
        ),
        "current_cost": round(
            current_cost,
            4,
        ),
        "lower_rate_cost": round(
            lower_rate_cost,
            4,
        ),
        "estimated_savings": round(
            estimated_savings,
            4,
        ),
        "estimated_savings_percent": round(
            estimated_savings_percent,
            1,
        ),
        "recommendation": recommendation,
        "recommended_start_time": (
            recommended_start_time
        ),
        "reason": reason,
    }

    print_json(result)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())

    except Exception as error:
        print_json(
            {
                "state": "error",
                "generated_at": datetime.now().isoformat(
                    timespec="seconds"
                ),
                "error": str(error),
            }
        )
        sys.exit(1)