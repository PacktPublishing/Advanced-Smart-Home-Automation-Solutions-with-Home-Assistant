"""
Filename: config_flow.py
Description: Configuration flow for the dual_measurement_sensor integration. Collects device ID, MQTT topics,
             optional JSON path for temperature, and filtering parameters such as debounce, hold, and smoothing.
Author: Marco Carvalho
Date: 2025-08-19
Version: 0.1.0
"""

from __future__ import annotations
from typing import Any
import voluptuous as vol

from homeassistant import config_entries
from homeassistant.data_entry_flow import FlowResult

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

# We keep the UI simple: two topics and a few filtering options.
# If readers later want JSON payloads, you can extend this with optional "json_key" fields.
STEP_USER_DATA_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_DEVICE_ID): str,  # used in unique_id and entity_ids
        vol.Required(CONF_TEMP_TOPIC): str,  # e.g. home/book2/dev1/temperature
        vol.Required(CONF_PRESENCE_TOPIC): str,  # e.g. home/book2/dev1/presence
        vol.Optional(CONF_TEMP_JSON_PATH, default=""): str,
        vol.Optional(CONF_TEMP_WINDOW, default=DEFAULT_TEMP_WINDOW): vol.All(
            int, vol.Range(min=1, max=60)
        ),
        vol.Optional(CONF_TEMP_MIN_DELTA, default=DEFAULT_TEMP_MIN_DELTA): vol.Coerce(
            float
        ),
        vol.Optional(CONF_DEBOUNCE_MS, default=DEFAULT_DEBOUNCE_MS): vol.All(
            int, vol.Range(min=0, max=10000)
        ),
        vol.Optional(CONF_HOLD_MS, default=DEFAULT_HOLD_MS): vol.All(
            int, vol.Range(min=0, max=600000)
        ),
    }
)


class ConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        if user_input is None:
            return self.async_show_form(
                step_id="user", data_schema=STEP_USER_DATA_SCHEMA
            )

        # Enforce one config entry per device_id
        await self.async_set_unique_id(f"{DOMAIN}:{user_input[CONF_DEVICE_ID]}")
        self._abort_if_unique_id_configured()

        return self.async_create_entry(
            title=f"{user_input[CONF_DEVICE_ID]}",
            data=user_input,
        )
