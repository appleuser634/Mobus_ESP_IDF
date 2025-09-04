// Lightweight Chat API client for ESP-IDF
// Matches web_server routes under /api/* and uses JWT auth

#pragma once

#include <string>
#include <cstring>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <ArduinoJson.h>

namespace chatapi {

static const char* CHAT_TAG = "ChatAPI";

// Simple helper to read/write strings to NVS
inline bool nvs_put_string(const char* key, const std::string& value) {
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_str(h, key, value.c_str());
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

inline std::string nvs_get_string(const char* key) {
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
    ChatApiClient(std::string serverHost = "localhost", int port = 8080)
        : host_(std::move(serverHost)), port_(port) {
        // Allow override from NVS if set
        std::string h = nvs_get_string("server_host");
        std::string p = nvs_get_string("server_port");
        if (!h.empty()) host_ = h;
        if (!p.empty()) {
            int v = atoi(p.c_str());
            if (v > 0) port_ = v;
        }
        token_ = nvs_get_string("jwt_token");
        user_id_ = nvs_get_string("user_id");
    }

    // POST /api/auth/login
    // On success saves token and user_id in NVS
    esp_err_t login(const std::string& username, const std::string& password) {
        StaticJsonDocument<256> body;
        body["username"] = username;
        body["password"] = password;
        std::string payload;
        payload.reserve(256);
        serializeJson(body, payload);

        std::string resp;
        auto err = request("POST", "/api/auth/login", payload, resp, /*auth*/false);
        if (err != ESP_OK) return err;

        StaticJsonDocument<384> doc;
        auto jerr = deserializeJson(doc, resp);
        if (jerr != DeserializationError::Ok) {
            ESP_LOGE(CHAT_TAG, "login: JSON parse error");
            return ESP_FAIL;
        }

        token_ = std::string(doc["token"].as<const char*>() ? doc["token"].as<const char*>() : "");
        user_id_ = std::string(doc["user_id"].as<const char*>() ? doc["user_id"].as<const char*>() : "");
        if (token_.empty() || user_id_.empty()) {
            ESP_LOGE(CHAT_TAG, "login: token or user_id missing");
            return ESP_FAIL;
        }
        nvs_put_string("jwt_token", token_);
        nvs_put_string("user_id", user_id_);
        nvs_put_string("username", username);
        return ESP_OK;
    }

    // POST /api/messages/send { receiver_id, content }
    esp_err_t send_message(const std::string& receiver_identifier, const std::string& content, std::string* out_response = nullptr) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        StaticJsonDocument<512> body;
        body["receiver_id"] = receiver_identifier;
        body["content"] = content;
        std::string payload;
        serializeJson(body, payload);
        std::string resp;
        auto err = request("POST", "/api/messages/send", payload, resp, /*auth*/true);
        if (err == ESP_OK && out_response) *out_response = resp;
        return err;
    }

    // GET /api/friends/:friend_id/messages?limit=n
    esp_err_t get_messages(const std::string& friend_identifier, int limit, std::string& out_response) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        char path[192];
        if (limit < 1 || limit > 100) limit = 50;
        snprintf(path, sizeof(path), "/api/friends/%s/messages?limit=%d", friend_identifier.c_str(), limit);
        return request("GET", path, "", out_response, /*auth*/true);
    }

    // GET /api/messages/unread/count
    esp_err_t get_unread_count(std::string& out_response) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        return request("GET", "/api/messages/unread/count", "", out_response, /*auth*/true);
    }

    // PUT /api/messages/:message_id/read
    esp_err_t mark_as_read(const std::string& message_id) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        std::string resp;
        std::string path = std::string("/api/messages/") + message_id + "/read";
        return request("PUT", path.c_str(), "", resp, /*auth*/true);
    }

    // PUT /api/friends/:friend_id/messages/read-all
    esp_err_t mark_all_as_read(const std::string& friend_identifier) {
        if (token_.empty()) return ESP_ERR_INVALID_STATE;
        std::string resp;
        std::string path = std::string("/api/friends/") + friend_identifier + "/messages/read-all";
        return request("PUT", path.c_str(), "", resp, /*auth*/true);
    }

    const std::string& user_id() const { return user_id_; }
    const std::string& token() const { return token_; }
    const std::string& host() const { return host_; }
    int port() const { return port_; }

private:
    std::string host_;
    int port_ = 8080;
    std::string token_;
    std::string user_id_;

    static esp_err_t _handle_events(esp_http_client_event_t* evt) {
        // No-op, but could log per-event
        return ESP_OK;
    }

    esp_err_t request(const char* method, const char* path, const std::string& body, std::string& out_resp, bool auth) {
        // Configure client per request (keep simple)
        esp_http_client_config_t cfg = {};
        cfg.host = host_.c_str();
        cfg.port = port_;
        cfg.path = path;
        cfg.event_handler = _handle_events;
        cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;

        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) return ESP_FAIL;

        esp_http_client_set_method(client, 
            strcmp(method, "POST") == 0 ? HTTP_METHOD_POST :
            strcmp(method, "PUT") == 0 ? HTTP_METHOD_PUT : HTTP_METHOD_GET);

        if (auth && !token_.empty()) {
            std::string authz = std::string("Bearer ") + token_;
            esp_http_client_set_header(client, "Authorization", authz.c_str());
        }
        if (!body.empty()) {
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, body.c_str(), body.size());
        }

        esp_err_t err = esp_http_client_perform(client);
        if (err != ESP_OK) {
            ESP_LOGE(CHAT_TAG, "HTTP %s %s failed: %s", method, path, esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }

        // Read response body
        int content_len = esp_http_client_get_content_length(client);
        if (content_len < 0) content_len = 0; // may be chunked
        char buf[1024];
        out_resp.clear();
        int read_len;
        do {
            read_len = esp_http_client_read(client, buf, sizeof(buf));
            if (read_len > 0) out_resp.append(buf, buf + read_len);
        } while (read_len > 0);

        esp_http_client_cleanup(client);
        return ESP_OK;
    }
};

} // namespace chatapi
