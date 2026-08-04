#!/usr/bin/env python3

import csv
import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Any


CSV_FILE = Path(
    "/config/www/dishwasher_hourly_energy.csv"
)

# Number of nearby historical records used
# for each prediction.
K_NEIGHBORS = 7

# Ignore extremely small measurements.
MINIMUM_ENERGY_KWH = 0.001

# Used only to determine whether the learned
# habit occurs during the higher-rate period.
HIGH_RATE_START_HOUR = 15
HIGH_RATE_END_HOUR = 19


def print_json(data: dict[str, Any]) -> None:
    """Print a dictionary as JSON."""

    print(
        json.dumps(
            data,
            ensure_ascii=False,
        )
    )


def circular_hour_distance(
    first_hour: int,
    second_hour: int,
) -> int:
    """
    Calculate the shortest distance between
    two hours on a 24-hour clock.
    """

    difference = abs(
        first_hour - second_hour
    )

    return min(
        difference,
        24 - difference,
    )


def circular_weekday_distance(
    first_day: int,
    second_day: int,
) -> int:
    """
    Calculate the shortest distance between
    two weekdays.
    """

    difference = abs(
        first_day - second_day
    )

    return min(
        difference,
        7 - difference,
    )


def load_dataset() -> list[dict[str, Any]]:
    """Load valid hourly records from the CSV file."""

    if not CSV_FILE.exists():
        return []

    records: list[dict[str, Any]] = []

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

                record = {
                    "timestamp": timestamp,
                    "hour": int(
                        row.get(
                            "hour",
                            timestamp.hour,
                        )
                    ),
                    "weekday": int(
                        row.get(
                            "weekday",
                            timestamp.weekday(),
                        )
                    ),
                    "is_weekend": int(
                        row.get(
                            "is_weekend",
                            timestamp.weekday() >= 5,
                        )
                    ),
                    "energy_kwh": float(
                        row["energy_kwh"]
                    ),
                }

            except (
                KeyError,
                TypeError,
                ValueError,
            ):
                continue

            records.append(record)

    return records


def calculate_distance(
    record: dict[str, Any],
    target_hour: int,
    target_weekday: int,
    target_is_weekend: int,
) -> float:
    """
    Calculate how similar a historical record
    is to the hour and day being predicted.
    """

    hour_distance = circular_hour_distance(
        record["hour"],
        target_hour,
    )

    weekday_distance = circular_weekday_distance(
        record["weekday"],
        target_weekday,
    )

    weekend_penalty = (
        0
        if record["is_weekend"]
        == target_is_weekend
        else 3
    )

    return (
        hour_distance
        + weekday_distance * 0.5
        + weekend_penalty
    )


def predict_energy(
    records: list[dict[str, Any]],
    target_hour: int,
    target_weekday: int,
    target_is_weekend: int,
) -> tuple[float, int]:
    """
    Predict energy for one hour using
    k-nearest-neighbors regression.
    """

    neighbors = []

    for record in records:
        distance = calculate_distance(
            record,
            target_hour,
            target_weekday,
            target_is_weekend,
        )

        neighbors.append(
            (
                distance,
                record["energy_kwh"],
            )
        )

    neighbors.sort(
        key=lambda item: item[0]
    )

    selected_neighbors = neighbors[
        :min(
            K_NEIGHBORS,
            len(neighbors),
        )
    ]

    weighted_energy = 0.0
    total_weight = 0.0
    usage_neighbors = 0

    for distance, energy_kwh in selected_neighbors:
        # A small value prevents division by zero
        # when the record exactly matches the target.
        weight = 1.0 / (
            distance + 0.1
        )

        weighted_energy += (
            energy_kwh * weight
        )

        total_weight += weight

        if energy_kwh >= MINIMUM_ENERGY_KWH:
            usage_neighbors += 1

    if total_weight == 0:
        return 0.0, 0

    predicted_energy = (
        weighted_energy / total_weight
    )

    return (
        predicted_energy,
        usage_neighbors,
    )


def main() -> int:
    """Learn the expected dishwasher usage hour."""

    records = load_dataset()

    usage_records = [
        record
        for record in records
        if record["energy_kwh"]
        >= MINIMUM_ENERGY_KWH
    ]

    if len(records) < 48:
        print_json(
            {
                "state": "insufficient_data",
                "generated_at": (
                    datetime.now().isoformat(
                        timespec="seconds"
                    )
                ),
                "sample_count": len(records),
                "usage_records": len(
                    usage_records
                ),
                "reason": (
                    "At least 48 hourly records "
                    "are required."
                ),
            }
        )
        return 0

    if len(usage_records) < 3:
        print_json(
            {
                "state": "insufficient_usage",
                "generated_at": (
                    datetime.now().isoformat(
                        timespec="seconds"
                    )
                ),
                "sample_count": len(records),
                "usage_records": len(
                    usage_records
                ),
                "reason": (
                    "At least three dishwasher "
                    "usage records are required."
                ),
            }
        )
        return 0

    now = datetime.now()
    target_weekday = now.weekday()
    target_is_weekend = int(
        target_weekday >= 5
    )

    predictions = []

    for hour in range(24):
        predicted_energy, support = (
            predict_energy(
                records,
                hour,
                target_weekday,
                target_is_weekend,
            )
        )

        predictions.append(
            {
                "hour": hour,
                "predicted_energy": (
                    predicted_energy
                ),
                "support": support,
            }
        )

    best_prediction = max(
        predictions,
        key=lambda item: item[
            "predicted_energy"
        ],
    )

    predicted_hour = best_prediction["hour"]
    predicted_energy = best_prediction[
        "predicted_energy"
    ]
    support = best_prediction["support"]

    support_percent = (
        support
        / min(
            K_NEIGHBORS,
            len(records),
        )
        * 100
    )

    predicted_during_high_rate = (
        HIGH_RATE_START_HOUR
        <= predicted_hour
        < HIGH_RATE_END_HOUR
    )

    if predicted_during_high_rate:
        state = "shift_recommended"

        recommendation = (
            "The learned dishwasher usage time "
            "falls within the higher-rate period. "
            f"Consider starting it at "
            f"{HIGH_RATE_END_HOUR:02d}:00 or later."
        )

        recommended_start_time = (
            f"{HIGH_RATE_END_HOUR:02d}:00"
        )

    else:
        state = "keep_current_time"

        recommendation = (
            "The learned dishwasher usage time "
            "already falls outside the higher-rate "
            "period."
        )

        recommended_start_time = (
            "keep_current_time"
        )

    output = {
        "state": state,
        "generated_at": now.isoformat(
            timespec="seconds"
        ),
        "predicted_hour": predicted_hour,
        "predicted_time": (
            f"{predicted_hour:02d}:00"
        ),
        "predicted_energy_kwh": round(
            predicted_energy,
            4,
        ),
        "sample_count": len(records),
        "usage_records": len(
            usage_records
        ),
        "neighbor_count": min(
            K_NEIGHBORS,
            len(records),
        ),
        "support_percent": round(
            support_percent,
            1,
        ),
        "predicted_during_high_rate": (
            predicted_during_high_rate
        ),
        "recommended_start_time": (
            recommended_start_time
        ),
        "recommendation": recommendation,
    }

    print_json(output)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())

    except Exception as error:
        print_json(
            {
                "state": "error",
                "generated_at": (
                    datetime.now().isoformat(
                        timespec="seconds"
                    )
                ),
                "error": str(error),
            }
        )

        sys.exit(1)