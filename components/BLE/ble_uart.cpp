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
#  if __has_include("esp_bt.h")
#    define HAVE_BT_HEADERS 1
#  else
#    define HAVE_BT_HEADERS 0
#  endif
#else
#  define HAVE_BT_HEADERS 0
#endif

// Forward declare NVS helpers (defined elsewhere)
std::string get_nvs(char* key);
void save_nvs(char* key, std::string record);

#include "include/ble_uart.hpp"

#define GATTS_TAG "BLE_UART"

// Shared state across implementations
static bool g_stack_inited = false;
static int g_last_err = 0;

// Network helpers used to free/recover memory around BLE usage
static void shutdown_network_stack()
{
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    (void)esp_event_loop_delete_default();
    (void)esp_netif_deinit();
}

static void restart_network_stack()
{
    if (esp_netif_init() != ESP_OK) return;
    (void)esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return;
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t conf = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        esp_wifi_set_config(WIFI_IF_STA, &conf);
    }
    (void)esp_wifi_start();
}

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BT_NIMBLE_ENABLED) && 0

#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// 128-bit UUIDs (big-endian for NimBLE macros)
static const ble_uuid128_t UUID_SERVICE = BLE_UUID128_INIT(0x6E,0x40,0x00,0x01,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E);
static const ble_uuid128_t UUID_RX      = BLE_UUID128_INIT(0x6E,0x40,0x00,0x02,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E);
static const ble_uuid128_t UUID_TX      = BLE_UUID128_INIT(0x6E,0x40,0x00,0x03,0xB5,0xA3,0xF3,0x93,0xE0,0xA9,0xE5,0x0E,0x24,0xDC,0xCA,0x9E);

static uint16_t tx_val_handle = 0;
static std::string rx_buffer;

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        const uint8_t* data = ctxt->om->om_data;
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        while (ctxt->om) {
            rx_buffer.append((const char*)ctxt->om->om_data, ctxt->om->om_len);
            ctxt->om = SLIST_NEXT(ctxt->om, om_next);
        }
        size_t idx;
        while ((idx = rx_buffer.find('\n')) != std::string::npos) {
            std::string frame = rx_buffer.substr(0, idx);
            rx_buffer.erase(0, idx + 1);
            if (!frame.empty()) {
                // Same handler as Bluedroid path
                ESP_LOGI(GATTS_TAG, "RXFrame: %s", frame.c_str());
                if (frame.find("\"type\"\s*:\s*\"new_message\"") != std::string::npos) {
                    save_nvs((char*)"notif_flag", std::string("true"));
                }
            }
        }
        (void)data; (void)len;
    }
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SERVICE.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &UUID_TX.u,
                .access_cb = gatt_access_cb,
                .val_handle = &tx_val_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = &UUID_RX.u,
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {0}
        },
        .includes = NULL,
    },
    { .type = 0, .uuid = NULL, .characteristics = NULL, .includes = NULL }
};

static void host_sync_cb(void)
{
    uint8_t addr_val[6];
    ble_hs_id_infer_auto(0, &addr_val[0]);
    ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr_val, nullptr);
    std::string name = "MoBus";
    std::string sid = get_nvs((char*)"short_id");
    if (sid.empty()) sid = get_nvs((char*)"ble_code");
    if (!sid.empty()) name += "-" + sid.substr(0, 8);
    ble_svc_gap_device_name_set(name.c_str());

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t*)name.c_str();
    fields.name_len = (uint8_t)name.size();
    fields.name_is_complete = 1;
    fields.uuids128 = (ble_uuid128_t *)&UUID_SERVICE;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    // Advertising
    struct ble_gap_adv_params advp = {};
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &advp, nullptr, nullptr);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(nullptr);
}

extern "C" void ble_uart_enable(void)
{
    g_last_err = 0;
    if (g_stack_inited) {
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, nullptr, nullptr, nullptr);
        return;
    }
    // Stop network to free memory
    shutdown_network_stack();

    int rc = esp_nimble_hci_and_controller_init();
    if (rc != 0) { g_last_err = rc; restart_network_stack(); return; }
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.sync_cb = host_sync_cb;

    nimble_port_freertos_init(nimble_host_task);
    g_stack_inited = true;
}

extern "C" void ble_uart_disable(void)
{
    if (g_stack_inited) {
        ble_gap_adv_stop();
        nimble_port_stop();
        nimble_port_deinit();
        esp_nimble_hci_and_controller_deinit();
        g_stack_inited = false;
    }
    restart_network_stack();
}

extern "C" int ble_uart_is_ready(void)
{
    // In NimBLE, readiness is based on connection + CCC; simplified to adv started
    return 1;
}

extern "C" int ble_uart_send(const uint8_t* data, size_t len)
{
    if (!g_stack_inited || tx_val_handle == 0) return -1;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return -2;
    int rc = ble_gatts_notify_custom(BLE_HS_CONN_HANDLE_NONE, tx_val_handle, om);
    return rc == 0 ? 0 : -3;
}

extern "C" int ble_uart_send_str(const char* s)
{
    if (!s) return -1;
    return ble_uart_send(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

extern "C" int ble_uart_last_err(void)
{
    return g_last_err;
}

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
static const uint8_t UUID_SERVICE[16] = {0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x01,0x00,0x40,0x6E};
static const uint8_t UUID_RX[16]      = {0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x02,0x00,0x40,0x6E};
static const uint8_t UUID_TX[16]      = {0x9E,0xCA,0xDC,0x24,0x0E,0xE5,0xA9,0xE0,0x93,0xF3,0xA3,0xB5,0x03,0x00,0x40,0x6E};

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
static uint16_t current_mtu = 23; // default
static bool adv_started = false;

// RX buffer to collect newline-terminated frames
static std::string rx_buffer;

// Attribute table
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

static esp_gatts_attr_db_t gatt_db[HRS_IDX_NB] = {
    // Service
    [IDX_SVC]      = { {ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&primary_service_uuid, ESP_GATT_PERM_READ, sizeof(UUID_SERVICE), sizeof(UUID_SERVICE), (uint8_t*)UUID_SERVICE} },
    // TX Characteristic (Notify)
    [IDX_TX_CHAR]  = { {ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&character_declaration_uuid, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t*)&char_prop_notify} },
    [IDX_TX_VAL]   = { {ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t*)UUID_TX, ESP_GATT_PERM_READ, 512, 0, nullptr} },
    [IDX_TX_CCC]   = { {ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&character_client_config_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE, sizeof(uint16_t), 0, nullptr} },
    // RX Characteristic (Write/WriteNR)
    [IDX_RX_CHAR]  = { {ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&character_declaration_uuid, ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t), (uint8_t*)&char_prop_write} },
    [IDX_RX_VAL]   = { {ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t*)UUID_RX, ESP_GATT_PERM_WRITE, 512, 0, nullptr} },
};

// Advertising data
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x40,
    .adv_int_max        = 0x80,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len = 0,
    .p_service_data = nullptr,
    .service_uuid_len = 16,
    .p_service_uuid = (uint8_t*)UUID_SERVICE,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};

static void start_advertising()
{
    if (adv_started) return;
    esp_err_t err = esp_ble_gap_start_advertising(&adv_params);
    if (err == ESP_OK) {
        adv_started = true;
    } else {
        ESP_LOGE(GATTS_TAG, "start adv failed: %s", esp_err_to_name(err));
    }
}

static void stop_advertising()
{
    if (!adv_started) return;
    esp_ble_gap_stop_advertising();
    adv_started = false;
}

static void handle_frame_from_phone(const std::string &frame)
{
    ESP_LOGI(GATTS_TAG, "RXFrame: %s", frame.c_str());
    if (frame.find("\"type\"\s*:\s*\"new_message\"") != std::string::npos) {
        save_nvs((char*)"notif_flag", std::string("true"));
    }
}

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        start_advertising();
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
                     esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT: {
        gatts_if_global = gatts_if;
        std::string name = "MoBus";
        std::string sid = get_nvs((char*)"short_id");
        if (sid.empty()) sid = get_nvs((char*)"ble_code");
        if (!sid.empty()) name += "-" + sid.substr(0, 8);
        esp_ble_gap_set_device_name(name.c_str());
        esp_ble_gap_config_adv_data(&adv_data);
        esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, HRS_IDX_NB, 0);
        break; }
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == ESP_GATT_OK && param->add_attr_tab.num_handle == HRS_IDX_NB) {
            memcpy(gatt_handle_table, param->add_attr_tab.handles, sizeof(gatt_handle_table));
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
        if (param->write.handle == gatt_handle_table[IDX_TX_CCC] && param->write.len == 2) {
            uint16_t v = param->write.value[1] << 8 | param->write.value[0];
            notify_enabled = (v != 0);
        } else if (param->write.handle == gatt_handle_table[IDX_RX_VAL]) {
            const uint8_t *d = param->write.value;
            uint16_t l = param->write.len;
            rx_buffer.append((const char*)d, (const char*)d + l);
            size_t idx;
            while ((idx = rx_buffer.find('\n')) != std::string::npos) {
                std::string frame = rx_buffer.substr(0, idx);
                rx_buffer.erase(0, idx + 1);
                if (!frame.empty()) handle_frame_from_phone(frame);
            }
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

static void shutdown_network_stack()
{
    // Stop MQTT is not accessible here; best-effort to stop Wi‑Fi + netif
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    (void)esp_event_loop_delete_default();
    (void)esp_netif_deinit();
}

static void restart_network_stack()
{
    // Re-init default netif and Wi‑Fi in STA mode; rely on saved config
    if (esp_netif_init() != ESP_OK) return;
    if (esp_event_loop_create_default() != ESP_OK) {
        // might already exist
    }
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return;
    esp_wifi_set_mode(WIFI_MODE_STA);
    // Use previously saved config from NVS
    wifi_config_t conf = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        esp_wifi_set_config(WIFI_IF_STA, &conf);
    }
    (void)esp_wifi_start();
}

extern "C" void ble_uart_enable(void)
{
    g_last_err = 0;
    if (g_stack_inited) { start_advertising(); return; }

    // Stop Wi‑Fi/network to free memory if running (ignore errors)
    shutdown_network_stack();
    (void)esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_err_t err;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) { ESP_LOGE(GATTS_TAG, "bt_controller_init failed: %s", esp_err_to_name(err)); g_last_err = err; goto fail; }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) { ESP_LOGE(GATTS_TAG, "bt_controller_enable failed: %s", esp_err_to_name(err)); g_last_err = err; goto fail_disable_ctrl; }

    err = esp_bluedroid_init();
    if (err != ESP_OK) { ESP_LOGE(GATTS_TAG, "bluedroid_init failed: %s", esp_err_to_name(err)); g_last_err = err; goto fail_disable_ctrl; }

    err = esp_bluedroid_enable();
    if (err != ESP_OK) { ESP_LOGE(GATTS_TAG, "bluedroid_enable failed: %s", esp_err_to_name(err)); g_last_err = err; goto fail_deinit_bluedroid; }

    err = esp_ble_gap_register_callback(gap_cb);
    if (err != ESP_OK) { ESP_LOGE(GATTS_TAG, "gap_register_callback failed: %s", esp_err_to_name(err)); g_last_err = err; goto fail_disable_bluedroid; }
    err = esp_ble_gatts_register_callback(gatts_cb);
    if (err != ESP_OK) { ESP_LOGE(GATTS_TAG, "gatts_register_callback failed: %s", esp_err_to_name(err)); g_last_err = err; goto fail_disable_bluedroid; }
    err = esp_ble_gatts_app_register(0x42);
    if (err != ESP_OK) { ESP_LOGE(GATTS_TAG, "gatts_app_register failed: %s", esp_err_to_name(err)); g_last_err = err; goto fail_disable_bluedroid; }

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
    restart_network_stack();
}

extern "C" void ble_uart_disable(void)
{
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

extern "C" int ble_uart_is_ready(void)
{
    return (conn_id_global != 0xFFFF) && notify_enabled ? 1 : 0;
}

extern "C" int ble_uart_last_err(void)
{
    return g_last_err;
}

extern "C" int ble_uart_send(const uint8_t* data, size_t len)
{
    if (!ble_uart_is_ready()) return -1;
    size_t mtu_payload = (current_mtu > 3) ? (current_mtu - 3) : 20;
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = std::min(mtu_payload, len - offset);
        esp_err_t err = esp_ble_gatts_send_indicate(
            gatts_if_global,
            conn_id_global,
            gatt_handle_table[IDX_TX_VAL],
            chunk,
            const_cast<uint8_t*>(data + offset),
            false);
        if (err != ESP_OK) return -2;
        offset += chunk;
    }
    return 0;
}

extern "C" int ble_uart_send_str(const char* s)
{
    if (!s) return -1;
    return ble_uart_send(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

#else

// Stub implementations when BT is disabled or headers unavailable
extern "C" void ble_uart_enable(void) {}
extern "C" void ble_uart_disable(void) {}
extern "C" int  ble_uart_is_ready(void) { return 0; }
extern "C" int  ble_uart_last_err(void) { return -1; }
extern "C" int  ble_uart_send(const uint8_t* data, size_t len) { (void)data; (void)len; return -1; }
extern "C" int  ble_uart_send_str(const char* s) { (void)s; return -1; }

#endif
