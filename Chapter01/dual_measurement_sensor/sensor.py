"""
Filename: sensor.py
Description: Temperature sensor entity for the dual_measurement_sensor integration. Provides smoothing
             via a moving average window and only publishes values when the change exceeds a configured delta.
Author: Marco Carvalho
Date: 2025-08-19
Version: 0.1.0
"""

from __future__ import annotations
from homeassistant.components.sensor import SensorEntity
from homeassistant.const import UnitOfTemperature
from homeassistant.core import HomeAssistant

from .const import DOMAIN, CONF_DEVICE_ID, CONF_TEMP_MIN_DELTA


async def async_setup_entry(hass, entry, async_add_entities):
    st = hass.data[DOMAIN][entry.entry_id]
    ent = DualTempSensor(hass, entry)
    # Each entity registers a callback so we can push updates when filters change
    st.setdefault("listeners", []).append(ent.async_write_ha_state)
    async_add_entities([ent])


class DualTempSensor(SensorEntity):
    """Temperature entity with simple smoothing and min-delta publishing."""

    _attr_has_entity_name = True
    _attr_name = "Temperature"
    _attr_native_unit_of_measurement = UnitOfTemperature.CELSIUS
    _attr_should_poll = False

    def __init__(self, hass, entry):
        self.hass = hass
        self.entry = entry
        self._attr_unique_id = f"{DOMAIN}:{entry.data[CONF_DEVICE_ID]}:temp"
        self._last_published = None  # remember last value we exposed

    @property
    def native_value(self):
        """Return a smoothed temperature, publishing only if change >= min delta."""
        st = self.hass.data[DOMAIN][self.entry.entry_id]
        f = st["filters"]
        val = st.get("last_temp_avg")
        if val is None:
            return None

        min_delta = self.entry.data.get(CONF_TEMP_MIN_DELTA, 0.1)

        # Only advance the value if it changed enough; this reduces chart noise.
        if self._last_published is None or abs(val - self._last_published) >= min_delta:
            self._last_published = val

        return self._last_published

    @property
    def device_info(self):
        return {
            "identifiers": {(DOMAIN, self.entry.data[CONF_DEVICE_ID])},
            "name": self.entry.data[CONF_DEVICE_ID],
            "manufacturer": "Book2",
            "model": "Dual Measurement Device",
        }
