import appdaemon.plugins.hass.hassapi as hass

class MotionLightTimer(hass.Hass):

    def initialize(self):
        self.handle = None
        self.listen_state(self.motion_detected, "binary_sensor.motion")

    def motion_detected(self, entity, attribute, old, new, kwargs):
        if new == "on":
            self.turn_on("light.living_room")

            if self.handle is not None:
                self.cancel_timer(self.handle)

            self.handle = self.run_in(self.turn_off_light, 10)

    def turn_off_light(self, kwargs):
        self.turn_off("light.living_room")
        self.handle = None