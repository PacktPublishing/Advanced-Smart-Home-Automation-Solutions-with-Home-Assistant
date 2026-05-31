import appdaemon.plugins.hass.hassapi as hass


class SecurityStateMachine(hass.Hass):
    """
    AppDaemon application that receives security decisions from Node-RED.

    Node-RED fires security_decision_event.
    This app updates the security status, stores the last message,
    sends notifications, and turns on lights for high-priority events.
    """

    # Initializes the AppDaemon application.
    # This function runs automatically when AppDaemon starts or reloads the app.
    # It registers the event listener and loads configuration values from apps.yaml.
    def initialize(self):
        self.listen_event(
            self.security_decision_received,
            self.args.get("event_name", "security_decision_event")
        )

        self.security_status_entity = self.args.get(
            "security_status_entity",
            "input_select.security_status"
        )

        self.last_message_entity = self.args.get(
            "last_message_entity",
            "input_text.last_security_message"
        )

        self.notify_services = self.args.get("notify_services", [])

        self.security_lights = self.args.get("security_lights", [])

        self.log("SecurityStateMachine initialized")

    # Processes the security decision received from Node-RED.
    # Expected fields are action, priority, security_state, reason,
    # home_status, motion_confirmed, and detected_at.
    # Based on these values, it updates the state and sends the proper alert.
    def security_decision_received(self, event_name, data, kwargs):
        action = data.get("action", "notify")
        priority = data.get("priority", "normal")
        security_state = data.get("security_state", "watching")
        reason = data.get("reason", "Security event detected")
        home_status = data.get("home_status", "unknown")
        motion_confirmed = data.get("motion_confirmed", False)
        detected_at = data.get("detected_at", "unknown time")

        self.log(
            f"Security decision received: action={action}, "
            f"priority={priority}, state={security_state}, reason={reason}"
        )

        self.set_security_status(security_state)
        self.set_last_security_message(reason)

        if action == "ignore":
            self.log("Security event ignored")
            return

        if priority in ["critical", "high"]:
            self.send_high_priority_alert(
                reason,
                home_status,
                motion_confirmed,
                detected_at
            )
            self.turn_on_security_lights()
            return

        title = "Low Priority Security Notice" if priority == "low" else "Security Notice"

        self.send_notification(
            title,
            reason,
            home_status,
            detected_at
        )

    # Updates the Home Assistant security status helper.
    def set_security_status(self, status):
        self.call_service(
            "input_select/select_option",
            entity_id=self.security_status_entity,
            option=status
        )

    # Updates the input_text helper used to show the last
    # security message on a Home Assistant dashboard.
    def set_last_security_message(self, message):
        if not self.entity_exists(self.last_message_entity):
            return

        self.call_service(
            "input_text/set_value",
            entity_id=self.last_message_entity,
            value=message[:255]
        )

    # Sends a standard notification using a dynamic title.
    # This function is used for both normal and low-priority alerts.
    def send_notification(self, title, reason, home_status, detected_at):
        message = f"{reason}. Home status: {home_status}. Time: {detected_at}"

        for notify_service in self.notify_services:
            self.call_service(
                notify_service,
                title=title,
                message=message
            )

    # Sends a high-priority alert to all configured phones.
    # This function is used when the priority is high or critical.
    def send_high_priority_alert(self, reason, home_status, motion_confirmed, detected_at):
        message = (
            f"{reason}. "
            f"Home status: {home_status}. "
            f"Motion confirmed: {motion_confirmed}. "
            f"Time: {detected_at}"
        )

        for notify_service in self.notify_services:
            self.call_service(
                notify_service,
                title="Security Alert",
                message=message,
                data={
                    "priority": "high",
                    "ttl": 0
                }
            )

    # Turns on the configured security lights and switches for high-priority events.
    # The list of devices is loaded from apps.yaml.
    # Light entities use light/turn_on and switch entities use switch/turn_on.
    def turn_on_security_lights(self):
        if not self.security_lights:
            self.log("No security lights configured")
            return

        light_entities = []
        switch_entities = []
        other_entities = []

        for entity_id in self.security_lights:
            if entity_id.startswith("light."):
                light_entities.append(entity_id)
            elif entity_id.startswith("switch."):
                switch_entities.append(entity_id)
            else:
                other_entities.append(entity_id) # for groups

        if light_entities:
            self.call_service(
                "light/turn_on",
                entity_id=light_entities
                #brightness_pct=80
            )

        if switch_entities:
            self.call_service(
                "switch/turn_on",
                entity_id=switch_entities
            )

        if other_entities:
            self.call_service(
                "homeassistant/turn_on",
                entity_id=other_entities
            )