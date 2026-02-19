#pragma once

namespace notification_bridge {

// Call when an external (non-MQTT) source reports a new message arrival.
// Ensures the unread counter is refreshed and notification effects fire.
void handle_external_message();

// Returns true if a new external notification hint was pending and clears it.
bool consume_external_hint();

}  // namespace notification_bridge
