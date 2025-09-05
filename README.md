# Mobus_ESP_IDF

### Open MenuConfig
`pio run -t menuconfig`

### Chat API and MQTT (added)
- New header `components/Network/include/chat_api.hpp` implements the web_server API:
  - `login(username, password)`
  - `send_message(receiver_id, content)`
  - `get_messages(friend_id_or_short_id, limit)`
  - `get_unread_count()`, `mark_as_read()`, `mark_all_as_read()`
- New header `components/Network/include/mqtt_client.hpp` subscribes to MQTT notifications on `chat/messages/<user_id>`.

Defaults target `http://localhost:8080` and `mqtt://localhost:1883`.
You can override via NVS keys:
- `server_host` (e.g. "192.168.0.10"), `server_port` (e.g. "8080")
- `mqtt_host` (e.g. "192.168.0.10")
- `user_name` and optional `password` (defaults to `password123` for development)

Existing `HttpClient` uses the new API internally for send/get and maps results to existing UI expectations.

### Bluetooth Pairing (UI)
- Settings now include a `Bluetooth` item that shows a simple pairing screen.
- Press the action button to start pairing; a 6‑digit code is displayed for 120 seconds. Press again to stop.
- BLE UART-like service is implemented with UUIDs in `web_server/BLUETOOTH_RELAY_SPEC.md`.
- The phone application MUST log in with the same account as this device. Use the same username/password you configured on the ESP32. See `web_server/BLUETOOTH_RELAY_SPEC.md` for the BLE relay protocol.

NVS keys used:
- `ble_pairing` = `"true"|"false"`
- `ble_pair_code` = 6‑digit string
- `ble_pair_expires_us` = expiration timestamp in microseconds since boot

Build note:
- Enable Bluetooth in menuconfig: `Component config -> Bluetooth -> Bluedroid Enable` and `BLE`.
- If disabled, BLE functions are no-op and only the UI is shown.
