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
