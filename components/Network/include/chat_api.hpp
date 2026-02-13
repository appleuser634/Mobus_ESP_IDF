// Lightweight Chat API client for ESP-IDF
// Matches web_server routes under /api/* and uses JWT auth

#pragma once

#include <string>
#include <cstring>
#include <stdlib.h>
#include <mutex>
#include <algorithm>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "nvs_flash.h"
#include "nvs.h"

#include <ArduinoJson.h>

namespace chatapi {

static const char* CHAT_TAG = "ChatAPI";

inline std::string url_encode(const std::string& input) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0xF]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}

inline void ensure_nvs_ready() {
    static bool inited = false;
    if (inited) return;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Do not erase here to avoid user data loss; try init again after erase only if absolutely required
        // For robustness in this client context, we attempt a safe erase+init.
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
    inited = true;
}

// Simple helper to read/write strings to NVS
inline bool nvs_put_string(const char* key, const std::string& value) {
    ensure_nvs_ready();
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(h, key, value.c_str());
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

inline std::string nvs_get_string(const char* key) {
    ensure_nvs_ready();
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READONLY, &h) != ESP_OK) return "";
    size_t len = 0;
    if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) {
        nvs_close(h);
        return "";
    }
    std::string s;
    s.resize(len);
    if (nvs_get_str(h, key, s.data(), &len) != ESP_OK) {
        nvs_close(h);
        return "";
    }
    nvs_close(h);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}


class ChatApiClient {
   public:
    ChatApiClient(std::string serverHost = "mimoc.jp", int port = 443)
        : default_host_(std::move(serverHost)),
          default_port_(port > 0 ? port : 443) {
        std::lock_guard<std::mutex> lock(mutex_);
        set_endpoint_locked(default_host_, default_port_);
        apply_nvs_endpoint_locked();
        token_ = nvs_get_string("jwt_token");
        user_id_ = nvs_get_string("user_id");
    }

    void reload_from_nvs(bool refresh_auth = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        set_endpoint_locked(default_host_, default_port_);
        apply_nvs_endpoint_locked();
        if (refresh_auth) {
            token_ = nvs_get_string("jwt_token");
            user_id_ = nvs_get_string("user_id");
        }
    }

    void set_endpoint(const std::string& host, int port,
                      const std::string& scheme = std::string()) {
        std::lock_guard<std::mutex> lock(mutex_);
        set_endpoint_locked(host, port, scheme);
    }

    void restore_default_endpoint() {
        std::lock_guard<std::mutex> lock(mutex_);
        set_endpoint_locked(default_host_, default_port_);
    }

    bool has_token() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !token_.empty();
    }

    std::string token() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return token_;
    }

    std::string user_id() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return user_id_;
    }

    std::string host() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return host_;
    }

    int port() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return port_;
    }

    std::string scheme() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return scheme_;
    }

    void set_scheme(const std::string& scheme) {
        if (scheme.empty()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        scheme_ = scheme;
    }

    esp_err_t login(const std::string& username,
                    const std::string& password) {
        auto ctx = snapshot();
        ESP_LOGI(CHAT_TAG, "login endpoint: %s://%s:%d",
                 ctx.scheme.c_str(), ctx.host.c_str(), ctx.port);

        std::string user_eff = username;
        if (user_eff.empty()) {
            user_eff = nvs_get_string("login_id");
            if (user_eff.empty()) user_eff = nvs_get_string("user_name");
            if (user_eff.empty()) user_eff = nvs_get_string("username");
        }
        std::string pass_eff = password;
        if (pass_eff.empty()) pass_eff = nvs_get_string("password");

        StaticJsonDocument<256> body;
        body["login_id"] = user_eff;
        body["password"] = pass_eff;
        std::string payload;
        payload.reserve(256);
        serializeJson(body, payload);

        std::string resp;
        int status = 0;
        auto err = perform_request("POST", "/api/auth/login", payload, resp,
                                   /*auth*/ false, &status);
        if (err != ESP_OK) return err;
        if (status >= 400) {
            ESP_LOGE(CHAT_TAG, "HTTP POST /api/auth/login status=%d body_len=%d",
                     status, (int)resp.size());
            return ESP_FAIL;
        }
        DynamicJsonDocument doc(resp.size() + 512);
        auto jerr = deserializeJson(doc, resp);
        if (jerr != DeserializationError::Ok) {
            ESP_LOGE(CHAT_TAG, "login: JSON parse error (len=%d)", (int)resp.size());
            return ESP_FAIL;
        }
        const char* tok = doc["token"].as<const char*>();
        if (!tok) tok = doc["access_token"].as<const char*>();
        if (!tok) tok = doc["jwt"].as<const char*>();
        const char* uid = doc["user_id"].as<const char*>();
        if (!uid && doc["user"].is<JsonObject>()) uid = doc["user"]["id"].as<const char*>();
        if (!uid) uid = doc["id"].as<const char*>();

        std::string new_token = tok ? std::string(tok) : std::string();
        std::string new_user_id = uid ? std::string(uid) : std::string();
        if (new_token.empty() || new_user_id.empty()) {
            ESP_LOGE(CHAT_TAG, "login: token or user_id missing");
            return ESP_FAIL;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            token_ = new_token;
            user_id_ = new_user_id;
        }

        nvs_put_string("jwt_token", new_token);
        nvs_put_string("user_id", new_user_id);
        nvs_put_string("login_id", user_eff);
        nvs_put_string("username", user_eff);
        std::string existing_name = nvs_get_string("user_name");
        if (existing_name.empty()) nvs_put_string("user_name", user_eff);
        const char* fc = doc["friend_code"].as<const char*>();
        if (!fc && doc["user"].is<JsonObject>()) fc = doc["user"]["friend_code"].as<const char*>();
        if (fc) nvs_put_string("friend_code", std::string(fc));
        const char* sid = doc["short_id"].as<const char*>();
        if (!sid && doc["user"].is<JsonObject>()) sid = doc["user"]["short_id"].as<const char*>();
        if (sid) nvs_put_string("short_id", std::string(sid));
        ESP_LOGI(CHAT_TAG, "login ok: token_len=%d", (int)new_token.size());
        return ESP_OK;
    }

    esp_err_t register_user(const std::string& login_id,
                            const std::string& nickname,
                            const std::string& password) {
        auto ctx = snapshot();
        ESP_LOGI(CHAT_TAG, "register endpoint: %s://%s:%d",
                 ctx.scheme.c_str(), ctx.host.c_str(), ctx.port);
        StaticJsonDocument<256> body;
        body["login_id"] = login_id;
        body["nickname"] = nickname.empty() ? login_id : nickname;
        body["password"] = password;
        std::string payload;
        serializeJson(body, payload);
        std::string resp;
        int status = 0;
        auto err = perform_request("POST", "/api/auth/register", payload, resp,
                                   /*auth*/ false, &status);
        if (err != ESP_OK) return err;
        if (status >= 400) {
            ESP_LOGE(CHAT_TAG, "HTTP POST /api/auth/register status=%d body='%s'",
                     status, resp.substr(0,160).c_str());
            return ESP_FAIL;
        }
        DynamicJsonDocument doc(resp.size() + 512);
        if (deserializeJson(doc, resp) != DeserializationError::Ok) {
            ESP_LOGE(CHAT_TAG, "register: JSON parse error");
            return ESP_FAIL;
        }
        const char* tok = doc["token"].as<const char*>();
        if (!tok) tok = doc["access_token"].as<const char*>();
        if (!tok) tok = doc["jwt"].as<const char*>();
        const char* uid = doc["user_id"].as<const char*>();
        if (!uid && doc["user"].is<JsonObject>()) uid = doc["user"]["id"].as<const char*>();
        if (!uid) uid = doc["id"].as<const char*>();
        std::string new_token = tok ? std::string(tok) : std::string();
        std::string new_user_id = uid ? std::string(uid) : std::string();
        if (new_token.empty() || new_user_id.empty()) {
            ESP_LOGE(CHAT_TAG, "register: token/user_id missing");
            return ESP_FAIL;
        }

        {
            std::lock_guard<std::mutex> guard(mutex_);
            token_ = new_token;
            user_id_ = new_user_id;
        }

        nvs_put_string("jwt_token", new_token);
        nvs_put_string("user_id", new_user_id);
        nvs_put_string("login_id", login_id);
        nvs_put_string("username", login_id);
        nvs_put_string("user_name", nickname.empty() ? login_id : nickname);
        const char* fc = doc["friend_code"].as<const char*>();
        if (!fc && doc["user"].is<JsonObject>()) fc = doc["user"]["friend_code"].as<const char*>();
        if (fc) nvs_put_string("friend_code", std::string(fc));
        const char* sid = doc["short_id"].as<const char*>();
        if (!sid && doc["user"].is<JsonObject>()) sid = doc["user"]["short_id"].as<const char*>();
        if (sid) nvs_put_string("short_id", std::string(sid));
        ESP_LOGI(CHAT_TAG, "register ok: token_len=%d", (int)new_token.size());
        return ESP_OK;
    }

    esp_err_t login_id_available(const std::string& login_id,
                                 bool* out_available) {
        if (!out_available) return ESP_ERR_INVALID_ARG;
        if (login_id.empty()) {
            *out_available = false;
            return ESP_ERR_INVALID_ARG;
        }
        std::string path = "/api/auth/login-id-available?login_id=" +
                           url_encode(login_id);
        std::string resp;
        int status = 0;
        auto err = perform_request("GET", path.c_str(), "", resp,
                                   /*auth*/ false, &status);
        if (err != ESP_OK) return err;
        if (status >= 400) return ESP_FAIL;
        DynamicJsonDocument doc(resp.size() + 128);
        auto jerr = deserializeJson(doc, resp);
        if (jerr != DeserializationError::Ok) return ESP_FAIL;
        if (!doc["available"].is<bool>()) return ESP_FAIL;
        *out_available = doc["available"].as<bool>();
        return ESP_OK;
    }

    esp_err_t send_message(const std::string& receiver_identifier,
                           const std::string& content,
                           std::string* out_response = nullptr) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        StaticJsonDocument<512> body;
        body["receiver_id"] = receiver_identifier;
        body["content"] = content;
        std::string payload;
        serializeJson(body, payload);
        std::string resp;
        auto err = perform_request("POST", "/api/messages/send", payload, resp,
                                   /*auth*/ true, nullptr);
        if (err == ESP_OK && out_response) *out_response = resp;
        return err;
    }

    esp_err_t get_messages(const std::string& friend_identifier, int limit,
                           std::string& out_response,
                           int* out_status = nullptr) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        char path[192];
        if (limit < 1 || limit > 100) limit = 50;
        snprintf(path, sizeof(path), "/api/friends/%s/messages?limit=%d",
                 friend_identifier.c_str(), limit);
        return perform_request("GET", path, "", out_response, /*auth*/ true,
                               out_status);
    }

    esp_err_t get_unread_count(std::string& out_response) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        return perform_request("GET", "/api/messages/unread/count", "", out_response,
                               /*auth*/ true, nullptr);
    }

    esp_err_t send_friend_request(const std::string& receiver_identifier,
                                  std::string* out_response, int* out_status) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        StaticJsonDocument<256> body;
        body["receiver_id"] = receiver_identifier;
        std::string payload;
        serializeJson(body, payload);
        std::string resp;
        int status = 0;
        auto err = perform_request("POST", "/api/friends/request", payload, resp,
                                   /*auth*/ true, &status);
        if (out_response) *out_response = resp;
        if (out_status) *out_status = status;
        return err;
    }

    esp_err_t mark_as_read(const std::string& message_id,
                           int* out_status = nullptr) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        std::string resp;
        std::string path = std::string("/api/messages/") + message_id + "/read";
        return perform_request("PUT", path.c_str(), "", resp, /*auth*/ true,
                               out_status);
    }

    esp_err_t mark_all_as_read(const std::string& friend_identifier) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        std::string resp;
        std::string path = std::string("/api/friends/") + friend_identifier +
                           "/messages/read-all";
        return perform_request("PUT", path.c_str(), "", resp, /*auth*/ true, nullptr);
    }

    esp_err_t get_friends(std::string& out_response, int* out_status = nullptr) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        return perform_request("GET", "/api/friends", "", out_response,
                               /*auth*/ true, out_status);
    }

    esp_err_t get_pending_requests(std::string& out_response,
                                   int* out_status = nullptr) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        return perform_request("GET", "/api/friends/pending", "",
                               out_response, /*auth*/ true, out_status);
    }

    esp_err_t respond_friend_request(const std::string& request_id, bool accept, std::string* out_response, int* out_status) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        StaticJsonDocument<256> body;
        body["request_id"] = request_id;
        body["accept"] = accept;
        std::string payload;
        serializeJson(body, payload);
        std::string resp;
        int status = 0;
        auto err = perform_request("POST", "/api/friends/respond", payload, resp, /*auth*/ true, &status);
        if (out_response) *out_response = resp;
        if (out_status) *out_status = status;
        return err;
    }

    esp_err_t refresh_friend_code(std::string& out_code, int* out_status = nullptr) {
        if (!has_token()) return ESP_ERR_INVALID_STATE;
        std::string resp; int status = 0;
        auto err = perform_request("POST", "/api/user/refresh-friend-code", "", resp, /*auth*/ true, &status);
        if (out_status) *out_status = status;
        if (err != ESP_OK) return err;
        DynamicJsonDocument doc(resp.size()+128);
        if (deserializeJson(doc, resp) != DeserializationError::Ok) return ESP_FAIL;
        const char* code = doc["friend_code"].as<const char*>();
        if (!code) return ESP_FAIL;
        out_code = code;
        nvs_put_string("friend_code", out_code);
        return ESP_OK;
    }

   private:
    struct EndpointSnapshot {
        std::string host;
        int port;
        std::string scheme;
        std::string token;
    };

    EndpointSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return EndpointSnapshot{host_, port_, scheme_, token_};
    }

    struct ResponseBuffer {
        std::string* out;
    };

    static esp_err_t _handle_events(esp_http_client_event_t* evt) {
        if (!evt) return ESP_OK;
        if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
            auto* buf = static_cast<ResponseBuffer*>(evt->user_data);
            if (buf && buf->out) {
                buf->out->append(static_cast<const char*>(evt->data), evt->data_len);
            }
        }
        return ESP_OK;
    }

    void set_endpoint_locked(const std::string& host, int port,
                             const std::string& scheme_override = std::string()) {
        host_ = host;
        int resolved_port = port > 0 ? port : default_port_;
        port_ = resolved_port;
        if (!scheme_override.empty()) {
            scheme_ = scheme_override;
        } else {
            scheme_ = (port_ == 443) ? "https" : "http";
        }
    }

    void apply_nvs_endpoint_locked() {
        std::string h = nvs_get_string("server_host");
        if (!h.empty()) host_ = std::move(h);
        std::string p = nvs_get_string("server_port");
        if (!p.empty()) {
            int v = atoi(p.c_str());
            if (v > 0) port_ = v;
        }
        std::string sch = nvs_get_string("server_scheme");
        if (!sch.empty()) {
            scheme_ = std::move(sch);
        } else if (scheme_.empty()) {
            scheme_ = (port_ == 443) ? "https" : "http";
        }
        // Ensure we have a sensible default if nothing is configured
        if (scheme_.empty()) {
            scheme_ = (port_ == 443) ? "https" : "http";
        }
    }

    esp_err_t perform_request(const char* method, const char* path,
                              const std::string& body, std::string& out_resp,
                              bool auth, int* out_status) {
        auto ctx = snapshot();
        std::string host = ctx.host;
        int port = ctx.port;
        std::string scheme = ctx.scheme;
        std::string token = ctx.token;

        esp_http_client_config_t cfg = {};
        ResponseBuffer resp_buf{&out_resp};
        std::string url = scheme + "://" + host + ":" + std::to_string(port) + path;
        cfg.url = url.c_str();
        cfg.event_handler = _handle_events;
        cfg.user_data = &resp_buf;
        cfg.transport_type = (scheme == "https") ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
        cfg.timeout_ms = request_timeout_ms_;
        cfg.keep_alive_enable = false;
        cfg.buffer_size = 2048;
        cfg.buffer_size_tx = 1024;
        if (is_ipv4_literal(host)) {
            cfg.skip_cert_common_name_check = true;
        }
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        if (scheme == "https") cfg.crt_bundle_attach = esp_crt_bundle_attach;
#else
        extern const unsigned char ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
        extern const unsigned char ca_cert_pem_end[]   asm("_binary_ca_cert_pem_end");
        if (scheme == "https") cfg.cert_pem = (const char*)ca_cert_pem_start;
#endif

        ESP_LOGI(CHAT_TAG, "HTTP %s %s", method, url.c_str());

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) return ESP_FAIL;

        esp_http_client_method_t httpMethod = HTTP_METHOD_GET;
        if (strcmp(method, "POST") == 0) httpMethod = HTTP_METHOD_POST;
        else if (strcmp(method, "PUT") == 0) httpMethod = HTTP_METHOD_PUT;
        esp_http_client_set_method(client, httpMethod);

        std::string authz;
        if (auth && !token.empty()) {
            authz = std::string("Bearer ") + token;
            esp_http_client_set_header(client, "Authorization", authz.c_str());
        }
        if (!body.empty()) {
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, body.c_str(), body.size());
        }

        esp_err_t err = esp_http_client_perform(client);
        if (err != ESP_OK) {
            ESP_LOGE(CHAT_TAG, "HTTP perform %s %s failed: %s", method, path,
                     esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }

        int status = esp_http_client_get_status_code(client);
        int content_len = esp_http_client_get_content_length(client);
        if (status >= 400) {
            std::string preview = out_resp.substr(0, std::min<size_t>(out_resp.size(), 160));
            ESP_LOGE(CHAT_TAG,
                     "HTTP %s %s status=%d len=%d body='%s'",
                     method, path, status, content_len, preview.c_str());
        } else {
            ESP_LOGD(CHAT_TAG,
                     "HTTP %s %s status=%d len=%d body_len=%d",
                     method, path, status, content_len, (int)out_resp.size());
        }

        esp_http_client_cleanup(client);
        if (out_status) *out_status = status;
        return ESP_OK;
    }

    mutable std::mutex mutex_;
    std::string host_;
    int port_ = 8080;
    std::string token_;
    std::string user_id_;
    std::string scheme_ = "http";
    std::string default_host_;
    int default_port_ = 8080;
    int request_timeout_ms_ = 15000;

    static bool is_ipv4_literal(const std::string& host) {
        if (host.empty()) return false;
        if (std::count(host.begin(), host.end(), '.') != 3) return false;
        for (char c : host) {
            if (c != '.' && (c < '0' || c > '9')) return false;
        }
        return true;
    }
};

struct Credentials {
    std::string username;
    std::string password;
};

inline Credentials load_credentials_from_nvs() {
    Credentials cred;
    cred.username = nvs_get_string("login_id");
    if (cred.username.empty()) cred.username = nvs_get_string("user_name");
    if (cred.username.empty()) cred.username = nvs_get_string("username");
    cred.password = nvs_get_string("password");
    if (cred.password.empty()) cred.password = "password123";
    return cred;
}

inline esp_err_t ensure_authenticated(ChatApiClient& api, const Credentials& cred,
                                      bool allow_register = true,
                                      bool force_refresh = false) {
    if (!force_refresh && api.has_token()) return ESP_OK;
    esp_err_t err = api.login(cred.username, cred.password);
    if (err == ESP_OK) return ESP_OK;
    if (!allow_register) return err;
    (void)api.register_user(cred.username, cred.username, cred.password);
    return api.login(cred.username, cred.password);
}

inline ChatApiClient& shared_client(bool refresh_from_nvs = false) {
    static ChatApiClient client;
    if (refresh_from_nvs) {
        client.reload_from_nvs();
    }
    return client;
}

}  // namespace chatapi
