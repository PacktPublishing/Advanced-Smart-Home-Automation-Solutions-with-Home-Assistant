#!/usr/bin/env python3

import sqlite3
import json
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

DB_PATH = "/config/home-assistant_v2.db"
TIMEZONE = ZoneInfo("America/Phoenix")
DAYS_BACK = 10
MIN_DURATION_MINUTES = 1

ENTITIES = [
    "binary_sensor.airfryer_running",
    "binary_sensor.dishwasher_running",
    "binary_sensor.wash_machine_running",
    "binary_sensor.sandwich_grill_on",
]

ENTITY_LABELS = {
    "binary_sensor.airfryer_running": "Air fryer",
    "binary_sensor.dishwasher_running": "Dishwasher",
    "binary_sensor.wash_machine_running": "Washing machine",
    "binary_sensor.sandwich_grill_on": "Sandwich grill",
}


def is_tep_off_peak(dt_local):
    """TEP Tucson simplified TOU schedule.
    Summer May-Sep: peak Mon-Fri 3 PM-7 PM.
    Winter Oct-Apr: peak Mon-Fri 6 AM-9 AM and 6 PM-9 PM.
    Weekends are off-peak.
    Holiday handling is not included in this simple example.
    """
    month = dt_local.month
    hour = dt_local.hour
    weekday = dt_local.weekday()  # Monday=0, Sunday=6

    if weekday >= 5:
        return True

    if 5 <= month <= 9:
        return not (15 <= hour < 19)

    return not ((6 <= hour < 9) or (18 <= hour < 21))


def tariff_label(dt_local):
    return "off_peak" if is_tep_off_peak(dt_local) else "peak"


def get_history_rows():
    start_epoch = (datetime.now(TIMEZONE) - timedelta(days=DAYS_BACK)).timestamp()
    placeholders = ",".join(["?"] * len(ENTITIES))

    query = f"""
        SELECT
            sm.entity_id,
            s.state,
            COALESCE(s.last_updated_ts, s.last_changed_ts) AS ts
        FROM states s
        JOIN states_meta sm ON s.metadata_id = sm.metadata_id
        WHERE sm.entity_id IN ({placeholders})
          AND COALESCE(s.last_updated_ts, s.last_changed_ts) >= ?
          AND s.state IN ('on', 'off')
        ORDER BY sm.entity_id, ts ASC;
    """

    conn = sqlite3.connect(DB_PATH)
    try:
        cur = conn.cursor()
        cur.execute(query, [*ENTITIES, start_epoch])
        return cur.fetchall()
    finally:
        conn.close()


def build_sessions(rows):
    active_starts = {}
    sessions = []

    for entity_id, state, ts in rows:
        if ts is None:
            continue

        dt_local = datetime.fromtimestamp(float(ts), TIMEZONE)

        if state == "on" and entity_id not in active_starts:
            active_starts[entity_id] = dt_local

        elif state == "off" and entity_id in active_starts:
            start = active_starts.pop(entity_id)
            end = dt_local
            duration = (end - start).total_seconds() / 60

            if duration >= MIN_DURATION_MINUTES:
                sessions.append({
                    "entity": entity_id,
                    "appliance": ENTITY_LABELS.get(entity_id, entity_id),
                    "start": start.strftime("%Y-%m-%d %H:%M"),
                    "end": end.strftime("%Y-%m-%d %H:%M"),
                    "duration_min": round(duration, 1),
                    "tariff": tariff_label(start),
                    "weekday": start.strftime("%A"),
                })

    return sessions


def build_summary(sessions):
    if not sessions:
        return "No appliance usage sessions were found in the selected history period."

    lines = [
        f"Appliance usage sessions from the last {DAYS_BACK} days.",
        "",
        "TEP tariff classification used:",
        "- Summer May-Sep peak: Monday-Friday, 3 PM-7 PM.",
        "- Winter Oct-Apr peak: Monday-Friday, 6 AM-9 AM and 6 PM-9 PM.",
        "- Weekends: off-peak.",
        "",
    ]

    for entity_id in ENTITIES:
        label = ENTITY_LABELS.get(entity_id, entity_id)
        entity_sessions = [s for s in sessions if s["entity"] == entity_id]

        lines.append(f"{label}:")
        if not entity_sessions:
            lines.append("- No sessions found.")
        else:
            for s in entity_sessions:
                lines.append(
                    f"- {s['weekday']} {s['start']} to {s['end']}, "
                    f"{s['duration_min']} min, {s['tariff']}"
                )
        lines.append("")

    return "\n".join(lines).strip()


def main():
    try:
        rows = get_history_rows()
        sessions = build_sessions(rows)
        summary = build_summary(sessions)

        output = {
            "state": "ready",
            "generated_at": datetime.now(TIMEZONE).strftime("%Y-%m-%d %H:%M:%S"),
            "days": DAYS_BACK,
            "session_count": len(sessions),
            "summary": summary,
        }

        print(json.dumps(output))

    except Exception as e:
        print(json.dumps({
            "state": "error",
            "generated_at": datetime.now(TIMEZONE).strftime("%Y-%m-%d %H:%M:%S"),
            "days": DAYS_BACK,
            "session_count": 0,
            "summary": f"Error generating appliance usage sessions: {e}",
        }))


if __name__ == "__main__":
    main()