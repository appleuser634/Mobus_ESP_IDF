// Minimal BLE GATT server implementing UART-like service

//
// Unified BLE implementation with compile-time fallback.
// - If SDK config enables Bluetooth and esp_bt headers are available,
//   compile the real GATT server implementation.
// - Otherwise, compile stubs so the app builds without Bluetooth.
//

#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

// Detect header availability
#if defined(__has_include)
#if __has_include("esp_bt.h")
#define HAVE_BT_HEADERS 1
#else
#define HAVE_BT_HEADERS 0
#endif
#else
#define HAVE_BT_HEADERS 0
#endif

// Use shared NVS helpers
#include <nvs_rw.hpp>

#include <notification_bridge.hpp>

#include "include/ble_uart.hpp"

// Forward declaration
static void handle_frame_from_phone(const std::string& frame);

#define GATTS_TAG "BLE_UART"

// Shared state across implementations
static bool g_stack_inited = false;
static bool g_net_suspended_for_ble = false;
static int g_last_err = 0;

// Network helpers used to free/recover memory around BLE usage
static void shutdown_network_stack() {
    // Minimal pause: stop Wi‑Fi driver only to free RAM.
    // Keep esp_netif and driver initialized to resume quickly.
    (void)esp_wifi_stop();
}

static void restart_network_stack() {
    // Resume Wi‑Fi driver if it was only stopped
    (void)esp_wifi_start();
}

static bool wifi_has_link() {
    wifi_ap_record_t ap = {};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

// Define shared frame handler outside of BLE implementation branches so
// both NimBLE and Bluedroid paths can link against it.
static void handle_frame_from_phone(const std::string& frame) {
    ESP_LOGI(GATTS_TAG, "RXFrame: %s", frame.c_str());
    if (frame.find("\"type\":\"new_message\"") != std::string::npos) {
        notification_bridge::handle_external_message();
    }
    // Cache friends/contacts list sent from phone app
    if (frame.find("\"type\":\"friends\"") != std::string::npos ||
        frame.find("\"type\":\"contacts\"") != std::string::npos) {
        save_nvs((char*)"ble_contacts", frame);
        ESP_LOGI(GATTS_TAG, "Saved friends list to NVS (ble_contacts)");
    }
    // Cache messages list for current friend
    if (frame.find("\"type\":\"messages\"") != std::string::npos) {
        save_nvs((char*)"ble_messages", frame);
        ESP_LOGI(GATTS_TAG, "Saved messages to NVS (ble_messages)");
    }
    // Fallback: if top-level contains a "messages" field, persist it even if type differs
    else if (frame.find("\"messages\"") != std::string::npos) {
        save_nvs((char*)"ble_messages", frame);
        ESP_LOGI(GATTS_TAG, "Saved messages (fallback) to NVS (ble_messages)");
    }
    // Cache pending requests list
    if (frame.find("\"type\":\"pending_requests\"") != std::string::npos ||
        frame.find("\"requests\"") != std::string::npos) {
        save_nvs((char*)"ble_pending", frame);
        ESP_LOGI(GATTS_TAG, "Saved pending requests to NVS (ble_pending)");
    }
    // Friend request results (send/accept/decline) -> store last result + id
    if (frame.find("\"type\":\"friend_request_result\"") != std::string::npos ||
        frame.find("\"type\":\"respond_friend_request_result\"") != std::string::npos) {
        // Extract simple id field if present:  "id":"...."
        std::string id;
        size_t p = frame.find("\"id\"");
        if (p != std::string::npos) {
            size_t colon = frame.find(':', p);
            if (colon != std::string::npos) {
                size_t q1 = frame.find('"', colon);
                if (q1 != std::string::npos) {
                    size_t q2 = frame.find('"', q1 + 1);
                    if (q2 != std::string::npos && q2 > q1 + 1) {
                        id = frame.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
        }
        if (!id.empty()) save_nvs((char*)"ble_result_id", id);
        save_nvs((char*)"ble_last_result", frame);
        ESP_LOGI(GATTS_TAG, "Saved BLE last result (id=%s)", id.c_str());
    }
}

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BT_NIMBLE_ENABLED)

#if __has_include("esp_nimble_hci.h")
#include "esp_nimble_hci.h"
#define HAVE_NIMBLE_HCI 1
#else
#define HAVE_NIMBLE_HCI 0
#endif
#include "esp_bt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#if defined(__has_include)
#if __has_include("host/store/ble_store_config.h")
#include "host/store/ble_store_config.h"
#define HAVE_BLE_STORE_CONFIG 1
#elif __has_include("store/ble_store_config.h")
#include "store/ble_store_config.h"
#define HAVE_BLE_STORE_CONFIG 1
#else
#define HAVE_BLE_STORE_CONFIG 0
#endif
#else
#define HAVE_BLE_STORE_CONFIG 0
#endif

// 128-bit UUIDs (big-endian for NimBLE macros)
// NimBLE expects 128-bit UUID bytes in little-endian order
static const ble_uuid128_t UUID_SERVICE =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3,
                     0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t UUID_RX =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3,
                     0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);
static const ble_uuid128_t UUID_TX =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3,
                     0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

static uint16_t tx_val_handle = 0;
static std::string rx_buffer;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool g_notify_enabled_flag = false;
static std::string g_pending_frame;

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // Append all fragments from mbuf chain
        struct os_mbuf* om = ctxt->om;
        while (om) {
            rx_buffer.append((const char*)om->om_data, om->om_len);
            om = SLIST_NEXT(om, om_next);
        }
        // First, handle newline-delimited frames
        size_t idx;
        while ((idx = rx_buffer.find('\n')) != std::string::npos) {
            std::string frame = rx_buffer.substr(0, idx);
            rx_buffer.erase(0, idx + 1);
            if (!frame.empty()) handle_frame_from_phone(frame);
        }
        // Also try to extract complete JSON objects without newline
        // by matching balanced braces outside quotes.
        auto extract_json = [&]() -> bool {
            size_t start = rx_buffer.find('{');
            if (start == std::string::npos) { rx_buffer.clear(); return false; }
            bool in_string = false; bool escape = false; int depth = 0;
            for (size_t i = start; i < rx_buffer.size(); ++i) {
                char c = rx_buffer[i];
                if (in_string) {
                    if (escape) { escape = false; }
                    else if (c == '\\') { escape = true; }
                    else if (c == '"') { in_string = false; }
                } else {
                    if (c == '"') in_string = true;
                    else if (c == '{') depth++;
                    else if (c == '}') { depth--; if (depth == 0) {
                        std::string frame = rx_buffer.substr(start, i - start + 1);
                        rx_buffer.erase(0, i + 1);
                        handle_frame_from_phone(frame);
                        return true;
                    }}
                }
            }
            // Not complete yet; keep buffer starting from '{'
            if (start > 0) rx_buffer.erase(0, start);
            return false;
        };
        // Extract as many complete JSON objects as possible
        int guard = 0;
        while (extract_json() && guard++ < 4) {}
    }
    return 0;
}

// Build GATT table at runtime to avoid C++ designated initializer issues
static bool g_gatt_registered = false;
static void gatt_build_and_register() {
    if (g_gatt_registered) return;
    static struct ble_gatt_chr_def chrs[3];
    memset(chrs, 0, sizeof(chrs));
    chrs[0].uuid = &UUID_TX.u;
    chrs[0].access_cb = gatt_access_cb;
    chrs[0].val_handle = &tx_val_handle;
    chrs[0].flags = BLE_GATT_CHR_F_NOTIFY;

    chrs[1].uuid = &UUID_RX.u;
    chrs[1].access_cb = gatt_access_cb;
    chrs[1].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;

    static struct ble_gatt_svc_def svcs[2];
    memset(svcs, 0, sizeof(svcs));
    svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    svcs[0].uuid = &UUID_SERVICE.u;
    svcs[0].characteristics = chrs;

    ble_gatts_count_cfg(svcs);
    ble_gatts_add_svcs(svcs);
    g_gatt_registered = true;
}

static int gap_event(struct ble_gap_event* event, void* arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                g_conn_handle = event->connect.conn_handle;
                ESP_LOGI(GATTS_TAG, "GAP connected; handle=%u", g_conn_handle);
            } else {
                ESP_LOGW(GATTS_TAG, "GAP connect fail; status=%d", event->connect.status);
                g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                g_notify_enabled_flag = false;
                // Resume advertising on failure
                uint8_t own_addr_type = 0;
                (void)ble_hs_id_infer_auto(0, &own_addr_type);
                struct ble_gap_adv_params advp = {};
                advp.conn_mode = BLE_GAP_CONN_MODE_UND;
                advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
                advp.itvl_min = 0x00A0;
                advp.itvl_max = 0x00F0;
                advp.channel_map = 0x07;
                (void)ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &advp, gap_event, nullptr);
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(GATTS_TAG, "GAP disconnected; reason=%d", event->disconnect.reason);
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            g_notify_enabled_flag = false;
            g_pending_frame.clear();
            // Restart advertising
            {
                uint8_t own_addr_type = 0;
                (void)ble_hs_id_infer_auto(0, &own_addr_type);
                struct ble_gap_adv_params advp = {};
                advp.conn_mode = BLE_GAP_CONN_MODE_UND;
                advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
                advp.itvl_min = 0x00A0;
                advp.itvl_max = 0x00F0;
                advp.channel_map = 0x07;
                (void)ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &advp, gap_event, nullptr);
            }
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            g_notify_enabled_flag = event->subscribe.cur_notify;
            ESP_LOGI(GATTS_TAG, "GAP subscribe: notify=%d handle=%u", (int)g_notify_enabled_flag, event->subscribe.conn_handle);
            if (g_notify_enabled_flag && !g_pending_frame.empty()) {
                // Flush pending frame now that notifications are enabled
                const std::string frame = g_pending_frame;
                g_pending_frame.clear();
                ble_uart_send(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
            }
            return 0;
        default:
            return 0;
    }
}

static void host_sync_cb(void) {
    // Determine own address type
    uint8_t own_addr_type = 0;
    (void)ble_hs_id_infer_auto(0, &own_addr_type);
    std::string name = "Mo-Bus";
    std::string sid = get_nvs((char*)"short_id");
    if (sid.empty()) sid = get_nvs((char*)"ble_code");
    if (!sid.empty()) name += "-" + sid.substr(0, 8);
    // Ensure services are registered at sync time
    gatt_build_and_register();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    // Make GATT db active
    (void)ble_gatts_start();
    ble_svc_gap_device_name_set(name.c_str());

    // Keep ADV payload minimal: flags + 128-bit service UUID only
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t*)&UUID_SERVICE;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(GATTS_TAG, "adv_set_fields failed: rc=%d", rc);
    }

    // Put device name into scan response to avoid 31-byte limit overflow
    struct ble_hs_adv_fields rsp = {};
    rsp.name = (uint8_t*)name.c_str();
    rsp.name_len = (uint8_t)name.size();
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(GATTS_TAG, "adv_rsp_set_fields failed: rc=%d", rc);
    }

    // Advertising
    struct ble_gap_adv_params advp;
    memset(&advp, 0, sizeof(advp));
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    // Set explicit, valid parameters to avoid controller errors
    advp.itvl_min = 0x00A0;  // 100 ms
    advp.itvl_max = 0x00F0;  // 150 ms
    advp.channel_map = 0x07; // Channels 37, 38, 39
    rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &advp,
                           gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(GATTS_TAG, "adv_start failed: rc=%d", rc);
    } else {
        ESP_LOGI(GATTS_TAG, "NimBLE advertising started (%s)", name.c_str());
    }
}

static void nimble_host_task(void* param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(nullptr);
}

static void on_reset_cb(int reason) {
    ESP_LOGE(GATTS_TAG, "NimBLE reset, reason=%d", reason);
}

extern "C" void ble_uart_enable(void) {
    g_last_err = 0;
    if (wifi_has_link()) {
        ESP_LOGW(GATTS_TAG, "Wi-Fi connected; skip BLE enable");
        g_last_err = ESP_ERR_INVALID_STATE;
        return;
    }
    if (g_stack_inited) {
        host_sync_cb();
        return;
    }
    // Keep Wi‑Fi/network running so HTTP APIs continue to work while BLE is active.
    // On ESP32-S3 with NimBLE there is enough memory; disabling network causes
    // app features (friends list fetch, MQTT, etc.) to fail.

    // Release Classic BT memory (host+controller)
    (void)esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    (void)esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT);

    // Initialize NimBLE host; it will set up controller/HCI internally.
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        // Retry once with network suspended to free RAM
        if (!g_net_suspended_for_ble) {
            shutdown_network_stack();
            g_net_suspended_for_ble = true;
            err = nimble_port_init();
        }
        if (err != ESP_OK) {
            g_last_err = err;
            ESP_LOGE(GATTS_TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
            if (g_net_suspended_for_ble) { restart_network_stack(); g_net_suspended_for_ble = false; }
            return;
        }
    }
    if (err != ESP_OK) {
        g_last_err = err;
        ESP_LOGE(GATTS_TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        // Network was not stopped; no restart needed
        return;
    }
    ESP_LOGI(GATTS_TAG, "nimble_port_init OK");
    // Configure host callbacks and persistent storage
    ble_hs_cfg.reset_cb = on_reset_cb;
    ble_hs_cfg.sync_cb = host_sync_cb;
#if HAVE_BLE_STORE_CONFIG
    ble_store_config_init();
#endif

    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(GATTS_TAG, "nimble_port_freertos_init OK");
    g_stack_inited = true;
    // If we temporarily stopped Wi‑Fi to free memory for BLE init, resume it
    if (g_net_suspended_for_ble) {
        restart_network_stack();
        g_net_suspended_for_ble = false;
    }
}

extern "C" void ble_uart_disable(void) {
    if (g_stack_inited) {
        ble_gap_adv_stop();
        nimble_port_stop();
        nimble_port_deinit();
        g_stack_inited = false;
    }
    if (g_net_suspended_for_ble) { restart_network_stack(); g_net_suspended_for_ble = false; }
}

extern "C" int ble_uart_is_ready(void) {
    // For simplicity, report ready after stack init; production should check
    // CCC.
    return g_stack_inited ? 1 : 0;
}

extern "C" int ble_uart_send(const uint8_t* data, size_t len) {
    if (!g_stack_inited || tx_val_handle == 0) return -1;
    if (!g_notify_enabled_flag || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        // Queue single pending frame until notifications are enabled
        g_pending_frame.assign(reinterpret_cast<const char*>(data), len);
        ESP_LOGI(GATTS_TAG, "Queue frame until notify enabled (%u bytes)", (unsigned)len);
        return 0;
    }
    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return -2;
    int rc = ble_gatts_notify_custom(g_conn_handle, tx_val_handle, om);
    return rc == 0 ? 0 : -3;
}

extern "C" int ble_uart_send_str(const char* s) {
    if (!s) return -1;
    return ble_uart_send(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

extern "C" int ble_uart_last_err(void) { return g_last_err; }

#elif defined(CONFIG_BT_ENABLED) && defined(CONFIG_BT_BLE_ENABLED)

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_bt_main.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

// 128-bit UUIDs in little-endian (ESP-IDF expects little-endian byte order)
static const uint8_t UUID_SERVICE[16] = {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5,
                                         0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5,
                                         0x01, 0x00, 0x40, 0x6E};
static const uint8_t UUID_RX[16] = {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5,
                                    0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5,
                                    0x02, 0x00, 0x40, 0x6E};
static const uint8_t UUID_TX[16] = {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5,
                                    0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5,
                                    0x03, 0x00, 0x40, 0x6E};

enum {
    IDX_SVC,
    IDX_TX_CHAR,
    IDX_TX_VAL,
    IDX_TX_CCC,
    IDX_RX_CHAR,
    IDX_RX_VAL,
    HRS_IDX_NB,
};

static uint16_t gatt_handle_table[HRS_IDX_NB] = {0};
static esp_gatt_if_t gatts_if_global = ESP_GATT_IF_NONE;
static uint16_t conn_id_global = 0xFFFF;
static bool notify_enabled = false;
static uint16_t current_mtu = 23;  // default
static bool adv_started = false;
// Track ADV/Scan Rsp config completion to start advertising at right time
static uint8_t adv_config_done = 0;
#define ADV_CONFIG_FLAG        (1 << 0)
#define SCAN_RSP_CONFIG_FLAG   (1 << 1)

// RX buffer to collect newline-terminated frames
static std::string rx_buffer;

// Attribute table
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid =
    ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_write =
    ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

static esp_gatts_attr_db_t gatt_db[HRS_IDX_NB] = {
    // Service
    [IDX_SVC] = {{ESP_GATT_AUTO_RSP},
                 {ESP_UUID_LEN_16, (uint8_t*)&primary_service_uuid,
                  ESP_GATT_PERM_READ, sizeof(UUID_SERVICE),
                  sizeof(UUID_SERVICE), (uint8_t*)UUID_SERVICE}},
    // TX Characteristic (Notify)
    [IDX_TX_CHAR] = {{ESP_GATT_AUTO_RSP},
                     {ESP_UUID_LEN_16, (uint8_t*)&character_declaration_uuid,
                      ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
                      (uint8_t*)&char_prop_notify}},
    [IDX_TX_VAL] = {{ESP_GATT_AUTO_RSP},
                    {ESP_UUID_LEN_128, (uint8_t*)UUID_TX, ESP_GATT_PERM_READ,
                     512, 0, nullptr}},
    [IDX_TX_CCC] = {{ESP_GATT_AUTO_RSP},
                    {ESP_UUID_LEN_16, (uint8_t*)&character_client_config_uuid,
                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t),
                     0, nullptr}},
    // RX Characteristic (Write/WriteNR)
    [IDX_RX_CHAR] = {{ESP_GATT_AUTO_RSP},
                     {ESP_UUID_LEN_16, (uint8_t*)&character_declaration_uuid,
                      ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
                      (uint8_t*)&char_prop_write}},
    [IDX_RX_VAL] = {{ESP_GATT_AUTO_RSP},
                    {ESP_UUID_LEN_128, (uint8_t*)UUID_RX, ESP_GATT_PERM_WRITE,
                     512, 0, nullptr}},
};

// Advertising data
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x40,
    .adv_int_max = 0x80,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Keep ADV payload minimal to avoid exceeding 31 bytes
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = false,
    .include_txpower = false,
    .min_interval = 0x0000,
    .max_interval = 0x0000,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len = 0,
    .p_service_data = nullptr,
    .service_uuid_len = 16,
    .p_service_uuid = (uint8_t*)UUID_SERVICE,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

// Put device name into Scan Response
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
};

static void start_advertising() {
    if (adv_started) return;
    esp_err_t err = esp_ble_gap_start_advertising(&adv_params);
    if (err == ESP_OK) {
        adv_started = true;
    } else {
        ESP_LOGE(GATTS_TAG, "start adv failed: %s", esp_err_to_name(err));
    }
}

static void stop_advertising() {
    if (!adv_started) return;
    esp_ble_gap_stop_advertising();
    adv_started = false;
}

// moved: handle_frame_from_phone is defined above to be shared

static void gap_cb(esp_gap_ble_cb_event_t event,
                   esp_ble_gap_cb_param_t* param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            adv_config_done &= ~ADV_CONFIG_FLAG;
            if (adv_config_done == 0) start_advertising();
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            adv_config_done &= ~SCAN_RSP_CONFIG_FLAG;
            if (adv_config_done == 0) start_advertising();
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(GATTS_TAG, "ADV start failed");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            break;
        default:
            break;
    }
}

static void gatts_cb(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                     esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            gatts_if_global = gatts_if;
            std::string name = "Mo-Bus";
            std::string sid = get_nvs((char*)"short_id");
            if (sid.empty()) sid = get_nvs((char*)"ble_code");
            if (!sid.empty()) name += "-" + sid.substr(0, 8);
            esp_ble_gap_set_device_name(name.c_str());
            adv_config_done = ADV_CONFIG_FLAG | SCAN_RSP_CONFIG_FLAG;
            esp_ble_gap_config_adv_data(&adv_data);
            // Ensure scan response is (re)configured to carry the name
            esp_ble_gap_config_adv_data(&scan_rsp_data);
            esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_IDX_NB, 0);
            break;
        }
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK &&
                param->add_attr_tab.num_handle == HRS_IDX_NB) {
                memcpy(gatt_handle_table, param->add_attr_tab.handles,
                       sizeof(gatt_handle_table));
                esp_ble_gatts_start_service(gatt_handle_table[IDX_SVC]);
            } else {
                ESP_LOGE(GATTS_TAG, "Create attr table failed");
            }
            break;
        case ESP_GATTS_CONNECT_EVT:
            conn_id_global = param->connect.conn_id;
            notify_enabled = false;
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            conn_id_global = 0xFFFF;
            notify_enabled = false;
            start_advertising();
            break;
        case ESP_GATTS_WRITE_EVT:
            if (param->write.handle == gatt_handle_table[IDX_TX_CCC] &&
                param->write.len == 2) {
                uint16_t v = param->write.value[1] << 8 | param->write.value[0];
                notify_enabled = (v != 0);
            } else if (param->write.handle == gatt_handle_table[IDX_RX_VAL]) {
                const uint8_t* d = param->write.value;
                uint16_t l = param->write.len;
                rx_buffer.append((const char*)d, (const char*)d + l);
                // Newline-delimited frames first
                size_t idx;
                while ((idx = rx_buffer.find('\n')) != std::string::npos) {
                    std::string frame = rx_buffer.substr(0, idx);
                    rx_buffer.erase(0, idx + 1);
                    if (!frame.empty()) handle_frame_from_phone(frame);
                }
                // Try balanced-JSON extraction without newline
                auto extract_json = [&]() -> bool {
                    size_t start = rx_buffer.find('{');
                    if (start == std::string::npos) { rx_buffer.clear(); return false; }
                    bool in_string = false; bool escape = false; int depth = 0;
                    for (size_t i = start; i < rx_buffer.size(); ++i) {
                        char c = rx_buffer[i];
                        if (in_string) {
                            if (escape) { escape = false; }
                            else if (c == '\\') { escape = true; }
                            else if (c == '"') { in_string = false; }
                        } else {
                            if (c == '"') in_string = true;
                            else if (c == '{') depth++;
                            else if (c == '}') { depth--; if (depth == 0) {
                                std::string frame = rx_buffer.substr(start, i - start + 1);
                                rx_buffer.erase(0, i + 1);
                                handle_frame_from_phone(frame);
                                return true;
                            }}
                        }
                    }
                    if (start > 0) rx_buffer.erase(0, start);
                    return false;
                };
                int guard = 0; while (extract_json() && guard++ < 4) {}
            }
            break;
        case ESP_GATTS_MTU_EVT:
            current_mtu = param->mtu.mtu;
            break;
        default:
            break;
    }
}

// g_stack_inited / g_last_err defined above (shared)

// Use the shared shutdown_network_stack()/restart_network_stack() defined above

extern "C" void ble_uart_enable(void) {
    g_last_err = 0;
    if (wifi_has_link()) {
        ESP_LOGW(GATTS_TAG, "Wi-Fi connected; skip BLE enable");
        g_last_err = ESP_ERR_INVALID_STATE;
        return;
    }
    if (g_stack_inited) {
        start_advertising();
        return;
    }

    // Keep network active to allow concurrent HTTP usage with BLE
    (void)esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_err_t err;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "bt_controller_init failed: %s",
                 esp_err_to_name(err));
        g_last_err = err;
        goto fail;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "bt_controller_enable failed: %s",
                 esp_err_to_name(err));
        g_last_err = err;
        goto fail_disable_ctrl;
    }

    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "bluedroid_init failed: %s", esp_err_to_name(err));
        g_last_err = err;
        goto fail_disable_ctrl;
    }

    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "bluedroid_enable failed: %s",
                 esp_err_to_name(err));
        g_last_err = err;
        goto fail_deinit_bluedroid;
    }

    err = esp_ble_gap_register_callback(gap_cb);
    if (err != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "gap_register_callback failed: %s",
                 esp_err_to_name(err));
        g_last_err = err;
        goto fail_disable_bluedroid;
    }
    err = esp_ble_gatts_register_callback(gatts_cb);
    if (err != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "gatts_register_callback failed: %s",
                 esp_err_to_name(err));
        g_last_err = err;
        goto fail_disable_bluedroid;
    }
    err = esp_ble_gatts_app_register(0x42);
    if (err != ESP_OK) {
        ESP_LOGE(GATTS_TAG, "gatts_app_register failed: %s",
                 esp_err_to_name(err));
        g_last_err = err;
        goto fail_disable_bluedroid;
    }

    g_stack_inited = true;
    start_advertising();
    return;

fail_disable_bluedroid:
    (void)esp_bluedroid_disable();
fail_deinit_bluedroid:
    (void)esp_bluedroid_deinit();
fail_disable_ctrl:
    (void)esp_bt_controller_disable();
    (void)esp_bt_controller_deinit();
    (void)esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
fail:
    // Best-effort restart Wi‑Fi so rest of app keeps working
    // Network was not stopped; keep as-is
}

extern "C" void ble_uart_disable(void) {
    stop_advertising();
    if (g_stack_inited) {
        (void)esp_bluedroid_disable();
        (void)esp_bluedroid_deinit();
        (void)esp_bt_controller_disable();
        (void)esp_bt_controller_deinit();
        (void)esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
        g_stack_inited = false;
    }
    restart_network_stack();
}

extern "C" int ble_uart_is_ready(void) {
    return (conn_id_global != 0xFFFF) && notify_enabled ? 1 : 0;
}

extern "C" int ble_uart_last_err(void) { return g_last_err; }

extern "C" int ble_uart_send(const uint8_t* data, size_t len) {
    if (!ble_uart_is_ready()) return -1;
    size_t mtu_payload = (current_mtu > 3) ? (current_mtu - 3) : 20;
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = std::min(mtu_payload, len - offset);
        esp_err_t err = esp_ble_gatts_send_indicate(
            gatts_if_global, conn_id_global, gatt_handle_table[IDX_TX_VAL],
            chunk, const_cast<uint8_t*>(data + offset), false);
        if (err != ESP_OK) return -2;
        offset += chunk;
    }
    return 0;
}

extern "C" int ble_uart_send_str(const char* s) {
    if (!s) return -1;
    return ble_uart_send(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

#else

// Stub implementations when BT is disabled or headers unavailable
extern "C" void ble_uart_enable(void) {}
extern "C" void ble_uart_disable(void) {}
extern "C" int ble_uart_is_ready(void) { return 0; }
extern "C" int ble_uart_last_err(void) { return -1; }
extern "C" int ble_uart_send(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return -1;
}
extern "C" int ble_uart_send_str(const char* s) {
    (void)s;
    return -1;
}

#endif
