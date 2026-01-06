"""
Filename: __init__.py
Description: Entry point for the dual_measurement_sensor integration. Sets up MQTT subscriptions
             for temperature and presence topics, manages shared state, and forwards setup to
             sensor and binary_sensor platforms.
Author: Marco Carvalho
Date: 2025-08-19
Version: 0.1.0
"""

from __future__ import annotations

import json
import logging
from collections import deque

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant, callback
from homeassistant.components import mqtt as mqtt_component

from .const import (
    DOMAIN,
    CONF_DEVICE_ID,
    CONF_TEMP_TOPIC,
    CONF_PRESENCE_TOPIC,
    CONF_TEMP_WINDOW,
    CONF_TEMP_MIN_DELTA,
    CONF_DEBOUNCE_MS,
    CONF_HOLD_MS,
    CONF_TEMP_JSON_PATH,
    DEFAULT_TEMP_WINDOW,
    DEFAULT_TEMP_MIN_DELTA,
    DEFAULT_DEBOUNCE_MS,
    DEFAULT_HOLD_MS,
)

_LOGGER = logging.getLogger(__name__)

PLATFORMS = [Platform.SENSOR, Platform.BINARY_SENSOR]


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up the dual_measurement_sensor integration from a config entry."""
    data = entry.data
    device_id: str = data[CONF_DEVICE_ID]
    temp_topic: str = data[CONF_TEMP_TOPIC]
    presence_topic: str = data[CONF_PRESENCE_TOPIC]
    temp_json_path = data.get(CONF_TEMP_JSON_PATH, "").strip()

    # Shared state for this config entry (read/written by MQTT handlers & entities)
    filt = {
        # Temperature smoothing & publish threshold
        "temp_window": data.get(CONF_TEMP_WINDOW, DEFAULT_TEMP_WINDOW),
        "temp_min_delta": data.get(CONF_TEMP_MIN_DELTA, DEFAULT_TEMP_MIN_DELTA),
        "temp_samples": deque(maxlen=data.get(CONF_TEMP_WINDOW, DEFAULT_TEMP_WINDOW)),
        "last_published_temp": None,
        # Presence debouncing & hold
        "presence_debounce_ms": data.get(CONF_DEBOUNCE_MS, DEFAULT_DEBOUNCE_MS),
        "presence_hold_ms": data.get(CONF_HOLD_MS, DEFAULT_HOLD_MS),
        "presence_state": False,
        "presence_last_on_ms": 0.0,
        "presence_last_change_ms": 0.0,
    }

    hass.data.setdefault(DOMAIN, {})
    hass.data[DOMAIN][entry.entry_id] = {
        "filters": filt,
        "device_id": device_id,
        "temp_topic": temp_topic,
        "presence_topic": presence_topic,
        "last_temp_avg": None,  # smoothed temp for the sensor entity
        "listeners": [],  # entities append their async_write_ha_state here
    }

# ---- helpers -------------------------------------------------------------

    def _extract_by_path(obj: dict, path: str):
        """Get a nested value from dict using dot path (e.g., 'BMP280.Temperature')."""
        if not path:
            return None
        cur = obj
        for part in path.split("."):
            if not isinstance(cur, dict) or part not in cur:
                return None
            cur = cur[part]
        return cur

    def _coerce_bool_string(v: str) -> bool | None:
        """Coerce common true/false strings to bool, else None."""
        s = v.strip().lower()
        if s in ("1", "true", "on", "yes"):
            return True
        if s in ("0", "false", "off", "no"):
            return False
        return None

    # MQTT message handlers (decode bytes → str, parse, update shared state, notify listeners)
    @callback
    def _handle_temp(msg) -> None:
        st = hass.data[DOMAIN][entry.entry_id]
        f = st["filters"]

        payload = msg.payload
        if isinstance(payload, (bytes, bytearray)):
            payload = payload.decode("utf-8", "ignore")
        s = str(payload).strip()

        value = None
        # Try simple scalar first
        try:
            value = float(s)
        except (TypeError, ValueError):
            # Try JSON with {"value": <number>}
            try:
                obj = json.loads(s)
                # 2a) top‑level "value"
                if value is None and isinstance(obj, dict) and "value" in obj:
                    value = float(obj["value"])
                # 2b) nested JSON via dot‑path (e.g. BMP280.Temperature)
                if value is None and isinstance(obj, dict) and temp_json_path:
                    v = _extract_by_path(obj, temp_json_path)
                    if v is not None:
                        value = float(v)
            except Exception:
                pass

        if value is None:
            _LOGGER.debug("Ignoring unparseable temperature payload: %r", payload)
            return

        f["temp_samples"].append(value)
        st["last_temp_avg"] = sum(f["temp_samples"]) / len(f["temp_samples"])

        # Immediately push state to any registered entities
        for cb in st["listeners"]:
            cb()

    @callback
    def _handle_presence(msg) -> None:
        st = hass.data[DOMAIN][entry.entry_id]
        f = st["filters"]

        payload = msg.payload
        if isinstance(payload, (bytes, bytearray)):
            payload = payload.decode("utf-8", "ignore")
        s = str(payload).strip().lower()

        want_on = s in ("1", "true", "on", "yes")

        # Use monotonic loop time for debounce/hold windows (milliseconds)
        now_ms = hass.loop.time() * 1000

        if want_on != f["presence_state"]:
            # Debounce: only accept a change if the last change was sufficiently long ago
            if now_ms - f["presence_last_change_ms"] >= f["presence_debounce_ms"]:
                f["presence_state"] = want_on
                f["presence_last_change_ms"] = now_ms
                if want_on:
                    f["presence_last_on_ms"] = now_ms
        else:
            # Hold: if ON, keep it on for at least presence_hold_ms
            if f["presence_state"] and (
                now_ms - f["presence_last_on_ms"] >= f["presence_hold_ms"]
            ):
                f["presence_state"] = False
                f["presence_last_change_ms"] = now_ms

        for cb in st["listeners"]:
            cb()

    # Subscribe to the topics using the MQTT component helper (Core-friendly)
    await mqtt_component.async_subscribe(hass, temp_topic, _handle_temp, qos=0)
    await mqtt_component.async_subscribe(hass, presence_topic, _handle_presence, qos=0)
    _LOGGER.debug(
        "Subscribed to %s (temp) and %s (presence) for %s",
        temp_topic,
        presence_topic,
        device_id,
    )

    # Set up platforms (entities will self-register their listeners in async_added_to_hass)
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    unload_ok = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unload_ok:
        hass.data[DOMAIN].pop(entry.entry_id, None)
    return unload_ok
