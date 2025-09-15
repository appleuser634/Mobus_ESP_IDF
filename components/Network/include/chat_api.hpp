// Lightweight Chat API client for ESP-IDF
// Matches web_server routes under /api/* and uses JWT auth

#pragma once

#include <string>
#include <cstring>
#include <stdlib.h>

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
    // serverHost: e.g. "localhost" or "mimoc.jp"
    // port: e.g. 8080
    // useTLS: not used yet (only HTTP for now)
    ChatApiClient(std::string serverHost = "mimoc.jp", int port = 443)
        : host_(std::move(serverHost)), port_(port) {
        // Allow override from NVS if set
        std::string h = nvs_get_string("server_host");
        std::string p = nvs_get_string("server_port");
        std::string sch = nvs_get_string("server_scheme");
        if (!h.empty()) host_ = h;
        if (!p.empty()) {
            int v = atoi(p.c_str());
            if (v > 0) port_ = v;
        }
        if (!sch.empty()) {
            scheme_ = sch; // "https" or "http"
        } else {
            // Infer scheme from port if not specified
            scheme_ = (port_ == 443) ? std::string("https") : std::string("http");
        }
        token_ = nvs_get_string("jwt_token");
        user_id_ = nvs_get_string("user_id");
    }

    // POST /api/auth/login
    // On success saves token and user_id in NVS
    esp_err_t login(const std::string& username, const std::string& password) {
        // Log endpoint for easier diagnosis
        ESP_LOGI(CHAT_TAG, "login endpoint: %s://%s:%d", scheme_.c_str(), host_.c_str(), port_);

        // Try multiple payload key variants for compatibility
        struct Variant { const char* user_key; const char* pass_key; };
        Variant variants[] = {
            {"Username","Password"},
            {"username","password"},
            {"user_name","password"},
            {"name","password"},
            {"email","password"},
        };

        // Fallback username from NVS if empty
        std::string user_eff = username;
        if (user_eff.empty()) {
            user_eff = nvs_get_string("user_name");
            if (user_eff.empty()) user_eff = nvs_get_string("username");
        }
        std::string pass_eff = password;
        if (pass_eff.empty()) pass_eff = nvs_get_string("password");

        for (auto &v : variants) {
            StaticJsonDocument<256> body;
            body[v.user_key] = user_eff;
            body[v.pass_key] = pass_eff;
            std::string payload; payload.reserve(256); serializeJson(body, payload);

            std::string resp; int status = 0;
            auto err = request_with_status("POST", "/api/auth/login", payload, resp, /*auth*/ false, &status);
            if (err != ESP_OK) continue;
            if (status >= 400) {
                ESP_LOGE(CHAT_TAG, "HTTP POST /api/auth/login status=%d body_len=%d", status, (int)resp.size());
                continue;
            }
            DynamicJsonDocument doc(resp.size() + 512);
            auto jerr = deserializeJson(doc, resp);
            if (jerr != DeserializationError::Ok) {
                ESP_LOGE(CHAT_TAG, "login: JSON parse error (len=%d)", (int)resp.size());
                continue;
            }
            // token: token|access_token|jwt
            const char* tok = doc["token"].as<const char*>();
            if (!tok) tok = doc["access_token"].as<const char*>();
            if (!tok) tok = doc["jwt"].as<const char*>();
            // user id: user_id|user.id|id
            const char* uid = doc["user_id"].as<const char*>();
            if (!uid && doc["user"].is<JsonObject>()) uid = doc["user"]["id"].as<const char*>();
            if (!uid) uid = doc["id"].as<const char*>();

            token_ = tok ? std::string(tok) : std::string("");
            user_id_ = uid ? std::string(uid) : std::string("");
            if (token_.empty() || user_id_.empty()) {
                ESP_LOGE(CHAT_TAG, "login: token or user_id missing (variant %s/%s)", v.user_key, v.pass_key);
                continue;
            }
            // Persist
            nvs_put_string("jwt_token", token_);
            nvs_put_string("user_id", user_id_);
            nvs_put_string("username", username);
            nvs_put_string("user_name", username);
            const char* fc = doc["friend_code"].as<const char*>();
            if (!fc && doc["user"].is<JsonObject>()) fc = doc["user"]["friend_code"].as<const char*>();
            if (fc) nvs_put_string("friend_code", std::string(fc));
            const char* sid = doc["short_id"].as<const char*>();
            if (!sid && doc["user"].is<JsonObject>()) sid = doc["user"]["short_id"].as<const char*>();
            if (sid) nvs_put_string("short_id", std::string(sid));
            ESP_LOGI(CHAT_TAG, "login ok: token_len=%d", (int)token_.size());
            return ESP_OK;
        }
        return ESP_FAIL;
    }

    // POST /api/auth/register
    // On success saves token and user_id in NVS
    esp_err_t register_user(const std::string& username,
                            const std::string& password) {
        ESP_LOGI(CHAT_TAG, "register endpoint: %s://%s:%d", scheme_.c_str(), host_.c_str(), port_);
        struct Variant { const char* user_key; const char* pass_key; };
        Variant variants[] = {
            {"Username","Password"},
            {"username","password"},
            {"user_name","password"},
            {"email","password"},
        };
        for (auto &v : variants) {
            StaticJsonDocument<256> body;
            body[v.user_key] = username;
            body[v.pass_key] = password;
            std::string payload; serializeJson(body, payload);
            std::string resp; int status = 0;
            auto err = request_with_status("POST", "/api/auth/register", payload, resp, /*auth*/ false, &status);
            if (err != ESP_OK) continue;
            if (status >= 400) { ESP_LOGE(CHAT_TAG, "HTTP POST /api/auth/register status=%d body='%s'", status, resp.substr(0,160).c_str()); continue; }
            DynamicJsonDocument doc(resp.size() + 512);
            if (deserializeJson(doc, resp) != DeserializationError::Ok) { ESP_LOGE(CHAT_TAG, "register: JSON parse error"); continue; }
            const char* tok = doc["token"].as<const char*>(); if (!tok) tok = doc["access_token"].as<const char*>(); if (!tok) tok = doc["jwt"].as<const char*>();
            const char* uid = doc["user_id"].as<const char*>(); if (!uid && doc["user"].is<JsonObject>()) uid = doc["user"]["id"].as<const char*>(); if (!uid) uid = doc["id"].as<const char*>();
            token_ = tok ? std::string(tok) : std::string("");
            user_id_ = uid ? std::string(uid) : std::string("");
            if (token_.empty() || user_id_.empty()) { ESP_LOGE(CHAT_TAG, "register: token/user_id missing (variant %s/%s)", v.user_key, v.pass_key); continue; }
            nvs_put_string("jwt_token", token_);
            nvs_put_string("user_id", user_id_);
            nvs_put_string("username", username);
            nvs_put_string("user_name", username);
            const char* fc = doc["friend_code"].as<const char*>(); if (!fc && doc["user"].is<JsonObject>()) fc = doc["user"]["friend_code"].as<const char*>(); if (fc) nvs_put_string("friend_code", std::string(fc));
            const char* sid = doc["short_id"].as<const char*>(); if (!sid && doc["user"].is<JsonObject>()) sid = doc["user"]["short_id"].as<const char*>(); if (sid) nvs_put_string("short_id", std::string(sid));
            ESP_LOGI(CHAT_TAG, "register ok: token_len=%d", (int)token_.size());
            return ESP_OK;
        }
        return ESP_FAIL;
    }

    // POST /api/messages/send { receiver_id, content }
    esp_err_t send_message(const std::string& receiver_identifier,
                           const std::string& content,
                           std::string* out_response = nullptr) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        StaticJsonDocument<512> body;
        body["receiver_id"] = receiver_identifier;
        body["content"] = content;
        std::string payload;
        serializeJson(body, payload);
        std::string resp;
        auto err =
            request("POST", "/api/messages/send", payload, resp, /*auth*/ true);
        if (err == ESP_OK && out_response) *out_response = resp;
        return err;
    }

    // GET /api/friends/:friend_id/messages?limit=n
    esp_err_t get_messages(const std::string& friend_identifier, int limit,
                           std::string& out_response) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        char path[192];
        if (limit < 1 || limit > 100) limit = 50;
        snprintf(path, sizeof(path), "/api/friends/%s/messages?limit=%d",
                 friend_identifier.c_str(), limit);
        return request("GET", path, "", out_response, /*auth*/ true);
    }

    // GET /api/messages/unread/count
    esp_err_t get_unread_count(std::string& out_response) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        return request("GET", "/api/messages/unread/count", "", out_response,
                       /*auth*/ true);
    }

    // POST /api/friends/request { receiver_id }
    // Returns esp_err_t and fills out_status + out_response
    esp_err_t send_friend_request(const std::string& receiver_identifier, std::string* out_response, int* out_status) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        StaticJsonDocument<256> body;
        body["receiver_id"] = receiver_identifier;
        std::string payload;
        serializeJson(body, payload);
        std::string resp;
        int status = 0;
        auto err = request_with_status("POST", "/api/friends/request", payload, resp, /*auth*/ true, &status);
        if (out_response) *out_response = resp;
        if (out_status) *out_status = status;
        return err;
    }

    // PUT /api/messages/:message_id/read
    esp_err_t mark_as_read(const std::string& message_id) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        std::string resp;
        std::string path = std::string("/api/messages/") + message_id + "/read";
        return request("PUT", path.c_str(), "", resp, /*auth*/ true);
    }

    // PUT /api/friends/:friend_id/messages/read-all
    esp_err_t mark_all_as_read(const std::string& friend_identifier) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        std::string resp;
        std::string path = std::string("/api/friends/") + friend_identifier +
                           "/messages/read-all";
        return request("PUT", path.c_str(), "", resp, /*auth*/ true);
    }

    const std::string& user_id() const { return user_id_; }
    const std::string& token() const { return token_; }
    const std::string& host() const { return host_; }
    int port() const { return port_; }

    // GET /api/friends -> {"friends":[{friend_id, short_id, username,
    // created_at}, ...]}
    esp_err_t get_friends(std::string& out_response) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        return request("GET", "/api/friends", "", out_response, /*auth*/ true);
    }

    // GET /api/friends/pending
    esp_err_t get_pending_requests(std::string& out_response) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        return request("GET", "/api/friends/pending", "", out_response, /*auth*/ true);
    }

    // POST /api/friends/respond { request_id, accept }
    esp_err_t respond_friend_request(const std::string& request_id, bool accept, std::string* out_response, int* out_status) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        StaticJsonDocument<256> body;
        body["request_id"] = request_id;
        body["accept"] = accept;
        std::string payload;
        serializeJson(body, payload);
        std::string resp;
        int status = 0;
        auto err = request_with_status("POST", "/api/friends/respond", payload, resp, /*auth*/ true, &status);
        if (out_response) *out_response = resp;
        if (out_status) *out_status = status;
        return err;
    }

    // POST /api/user/refresh-friend-code -> {"friend_code":"XXXXXXXX"}
    esp_err_t refresh_friend_code(std::string& out_code, int* out_status = nullptr) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        std::string resp; int status = 0;
        auto err = request_with_status("POST", "/api/user/refresh-friend-code", "", resp, /*auth*/ true, &status);
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
    std::string host_;
    int port_ = 8080;
    std::string token_;
    std::string user_id_;
    std::string scheme_ = "http";

    static esp_err_t _handle_events(esp_http_client_event_t* evt) {
        // No-op, but could log per-event
        return ESP_OK;
    }

    esp_err_t request(const char* method, const char* path,
                      const std::string& body, std::string& out_resp,
                      bool auth) {
        // Configure client per request (explicit open/write/read)
        esp_http_client_config_t cfg = {};
        cfg.host = host_.c_str();
        cfg.port = port_;
        cfg.path = path;
        cfg.event_handler = _handle_events; // no-op
        cfg.transport_type = (scheme_ == "https") ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
        cfg.timeout_ms = 5000;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        if (scheme_ == "https") cfg.crt_bundle_attach = esp_crt_bundle_attach;
#else
        // Use embedded CA certificate if bundle is not enabled
        extern const unsigned char ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
        extern const unsigned char ca_cert_pem_end[]   asm("_binary_ca_cert_pem_end");
        if (scheme_ == "https") cfg.cert_pem = (const char*)ca_cert_pem_start;
#endif

        ESP_LOGI(CHAT_TAG, "HTTP %s %s://%s:%d%s", method, scheme_.c_str(), host_.c_str(), port_, path);
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) return ESP_FAIL;

        esp_http_client_method_t httpMethod = HTTP_METHOD_GET;
        if (strcmp(method, "POST") == 0) httpMethod = HTTP_METHOD_POST;
        else if (strcmp(method, "PUT") == 0) httpMethod = HTTP_METHOD_PUT;
        esp_http_client_set_method(client, httpMethod);

        if (auth && !token_.empty()) {
            std::string authz = std::string("Bearer ") + token_;
            esp_http_client_set_header(client, "Authorization", authz.c_str());
        }
        if (!body.empty()) {
            esp_http_client_set_header(client, "Content-Type", "application/json");
        }

        esp_err_t err = esp_http_client_open(client, body.empty() ? 0 : body.size());
        if (err != ESP_OK) {
            ESP_LOGE(CHAT_TAG, "HTTP open %s %s failed: %s", method, path, esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }

        if (!body.empty()) {
            int wlen = esp_http_client_write(client, body.c_str(), body.size());
            if (wlen < 0) {
                ESP_LOGE(CHAT_TAG, "HTTP write failed");
                esp_http_client_cleanup(client);
                return ESP_FAIL;
            }
        }

        // Fetch headers to allow reading status
        (void)esp_http_client_fetch_headers(client);

        out_resp.clear();
        char buf[512];
        while (1) {
            int r = esp_http_client_read(client, buf, sizeof(buf));
            if (r <= 0) break;
            out_resp.append(buf, buf + r);
        }

        int status = esp_http_client_get_status_code(client);
        if (status >= 400) {
            ESP_LOGE(CHAT_TAG, "HTTP %s %s status=%d body_len=%d", method, path, status, (int)out_resp.size());
        } else {
            ESP_LOGD(CHAT_TAG, "HTTP %s %s status=%d body_len=%d", method, path, status, (int)out_resp.size());
        }

        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_OK;
    }

    // Same as request but captures HTTP status code
    esp_err_t request_with_status(const char* method, const char* path, const std::string& body,
                                  std::string& out_resp, bool auth, int* out_status) {
        esp_http_client_config_t cfg = {};
        cfg.host = host_.c_str();
        cfg.port = port_;
        cfg.path = path;
        cfg.event_handler = _handle_events;
        cfg.transport_type = (scheme_ == "https") ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
        cfg.timeout_ms = 5000;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        if (scheme_ == "https") cfg.crt_bundle_attach = esp_crt_bundle_attach;
#else
        extern const unsigned char ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
        extern const unsigned char ca_cert_pem_end[]   asm("_binary_ca_cert_pem_end");
        if (scheme_ == "https") cfg.cert_pem = (const char*)ca_cert_pem_start;
#endif

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) return ESP_FAIL;

        esp_http_client_method_t httpMethod = HTTP_METHOD_GET;
        if (strcmp(method, "POST") == 0) httpMethod = HTTP_METHOD_POST;
        else if (strcmp(method, "PUT") == 0) httpMethod = HTTP_METHOD_PUT;
        esp_http_client_set_method(client, httpMethod);

        if (auth && !token_.empty()) {
            std::string authz = std::string("Bearer ") + token_;
            esp_http_client_set_header(client, "Authorization", authz.c_str());
        }
        if (!body.empty()) esp_http_client_set_header(client, "Content-Type", "application/json");

        esp_err_t err = esp_http_client_open(client, body.empty() ? 0 : body.size());
        if (err != ESP_OK) {
            esp_http_client_cleanup(client);
            return err;
        }
        if (!body.empty()) {
            int wlen = esp_http_client_write(client, body.c_str(), body.size());
            if (wlen < 0) { esp_http_client_cleanup(client); return ESP_FAIL; }
        }
        (void)esp_http_client_fetch_headers(client);
        out_resp.clear();
        char buf[512];
        while (1) {
            int r = esp_http_client_read(client, buf, sizeof(buf));
            if (r <= 0) break;
            out_resp.append(buf, buf + r);
        }
        int status = esp_http_client_get_status_code(client);
        if (status >= 400) {
            // Log first bytes of body for diagnosis (truncated)
            std::string preview = out_resp.substr(0, std::min<size_t>(out_resp.size(), 160));
            ESP_LOGE(CHAT_TAG, "HTTP %s %s status=%d body='%s'", method, path, status, preview.c_str());
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (out_status) *out_status = status;
        return ESP_OK;
    }
};

}  // namespace chatapi
