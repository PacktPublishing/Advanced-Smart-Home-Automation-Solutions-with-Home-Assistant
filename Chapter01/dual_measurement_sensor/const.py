"""
Filename: const.py
Description: Defines constants and default values used across the dual_measurement_sensor integration,
             including MQTT topics, config keys, and default thresholds for temperature and presence.
Author: Marco Carvalho
Date: 2025-08-19
Version: 0.1.0
"""

DOMAIN = "dual_measurement_sensor"

# Topics (one for each signal coming from the same device)
CONF_TEMP_TOPIC = "temperature_topic"
CONF_PRESENCE_TOPIC = "presence_topic"

# Filtering knobs (safe defaults for book readers)
CONF_TEMP_WINDOW = "temperature_window"  # moving average window length
CONF_TEMP_MIN_DELTA = "temperature_min_delta"  # publish only if change >= delta
CONF_DEBOUNCE_MS = "presence_debounce_ms"  # ignore flicker shorter than this
CONF_HOLD_MS = "presence_hold_ms"  # keep 'on' for at least this long
CONF_DEVICE_ID = "device_id"
CONF_TEMP_JSON_PATH = "temperature_json_path"

DEFAULT_TEMP_WINDOW = 5
DEFAULT_TEMP_MIN_DELTA = 0.1
DEFAULT_DEBOUNCE_MS = 300
DEFAULT_HOLD_MS = 5000
