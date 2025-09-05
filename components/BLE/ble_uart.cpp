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

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BT_BLE_ENABLED)

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_bt_main.h"

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
        if (sid.empty()) sid = get_nvs((char*)"ble_pair_code");
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

static bool stack_inited = false;

extern "C" void ble_uart_enable(void)
{
    if (!stack_inited) {
        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
        esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
        ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
        ESP_ERROR_CHECK(esp_bluedroid_init());
        ESP_ERROR_CHECK(esp_bluedroid_enable());
        ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_cb));
        ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_cb));
        ESP_ERROR_CHECK(esp_ble_gatts_app_register(0x42));
        stack_inited = true;
    } else {
        start_advertising();
    }
}

extern "C" void ble_uart_disable(void)
{
    stop_advertising();
}

extern "C" int ble_uart_is_ready(void)
{
    return (conn_id_global != 0xFFFF) && notify_enabled ? 1 : 0;
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
extern "C" int  ble_uart_send(const uint8_t* data, size_t len) { (void)data; (void)len; return -1; }
extern "C" int  ble_uart_send_str(const char* s) { (void)s; return -1; }

#endif
