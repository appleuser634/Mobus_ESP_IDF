#include "notification_bridge.hpp"

#include <atomic>

#include "notification_effects.hpp"

namespace notification_bridge {

namespace {
std::atomic<bool> g_external_unread_hint{false};
}

void handle_external_message() {
    g_external_unread_hint.store(true);
    notification_effects::signal_new_message();
}

bool consume_external_hint() {
    return g_external_unread_hint.exchange(false);
}

}  // namespace notification_bridge
