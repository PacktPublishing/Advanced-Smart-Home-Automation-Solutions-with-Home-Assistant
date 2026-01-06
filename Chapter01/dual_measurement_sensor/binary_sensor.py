"""
Filename: binary_sensor.py
Description: Binary sensor entity for presence detection in the dual_measurement_sensor integration.
             Implements debounce and hold logic and updates state via MQTT callbacks.
Author: Marco Carvalho
Date: 2025-08-19
Version: 0.1.0
"""

from __future__ import annotations
from homeassistant.components.binary_sensor import (
    BinarySensorEntity,
    BinarySensorDeviceClass,
)
# from homeassistant.core import HomeAssistant  # unused; remove

from .const import DOMAIN, CONF_DEVICE_ID


async def async_setup_entry(hass, entry, async_add_entities):
    ent = DualPresenceBS(hass, entry)
    # Don't append listeners here; do it in async_added_to_hass()
    async_add_entities([ent])


class DualPresenceBS(BinarySensorEntity):
    """Presence entity with debounce and hold logic baked in."""

    _attr_has_entity_name = True
    _attr_name = "Presence"
    _attr_device_class = BinarySensorDeviceClass.MOTION
    _attr_should_poll = False  # push-updated

    def __init__(self, hass, entry):
        self.hass = hass
        self.entry = entry
        self._attr_unique_id = f"{DOMAIN}:{entry.data[CONF_DEVICE_ID]}:presence"

    async def async_added_to_hass(self) -> None:
        st = self.hass.data[DOMAIN][self.entry.entry_id]
        listeners = st.setdefault("listeners", [])
        if self.async_write_ha_state not in listeners:
            listeners.append(self.async_write_ha_state)
        # write an initial state if already known
        self.async_write_ha_state()

    @property
    def is_on(self) -> bool | None:
        st = self.hass.data[DOMAIN][self.entry.entry_id]
        return st["filters"]["presence_state"]

    @property
    def device_info(self):
        return {
            "identifiers": {(DOMAIN, self.entry.data[CONF_DEVICE_ID])},
            "name": self.entry.data[CONF_DEVICE_ID],
            "manufacturer": "marcoascarvalho",
            "model": "Dual Measurement Device",
        }
