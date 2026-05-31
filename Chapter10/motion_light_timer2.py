import appdaemon.plugins.hass.hassapi as hass 

class MotionLightTimer2(hass.Hass): 

    def initialize(self): 
        self.handle = None 
        self.motion_sensor = self.args["motion_sensor"] 
        self.target_light = self.args["target_light"] 
        self.timeout = self.args.get("timeout", 300) 
        self.listen_state(self.motion_detected, self.motion_sensor) 

    def motion_detected(self, entity, attribute, old, new, kwargs): 
        if new == "on": 
            self.turn_on(self.target_light) 

            if self.handle is not None: 
                self.cancel_timer(self.handle) 

            self.handle = self.run_in(self.turn_off_light, self.timeout) 

    def turn_off_light(self, kwargs): 
        self.turn_off(self.target_light) 
        self.handle = None 