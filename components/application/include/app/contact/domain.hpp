#pragma once

#include <cstdlib>
#include <cstring>
#include <string>

#include <ArduinoJson.h>

namespace app::contactbook {

struct ContactEntry {
    std::string display_name;  // nickname (fallback to username)
    std::string identifier;    // preferred identifier
    std::string username;      // login id for contact
    std::string short_id;
    std::string friend_id;
    int unread_count = 0;
    bool has_unread = false;
};

inline int parse_unread_count(JsonVariantConst node) {
    int count = 0;
    JsonVariantConst unread_count = node["unread_count"];
    if (!unread_count.isNull()) {
        if (unread_count.is<const char*>()) {
            const char* s = unread_count.as<const char*>();
            count = s ? std::atoi(s) : 0;
        } else {
            count = unread_count.as<int>();
        }
    } else if (node["unread"].is<int>()) {
        count = node["unread"].as<int>();
    } else if (node["has_unread"].is<bool>()) {
        count = node["has_unread"].as<bool>() ? 1 : 0;
    }
    return count < 0 ? 0 : count;
}

inline ContactEntry make_contact_entry(JsonVariantConst node) {
    ContactEntry c;
    const char* uname = node["username"].as<const char*>();
    if (!uname || std::strlen(uname) == 0) {
        uname = node["user_name"].as<const char*>();
    }
    if (!uname || std::strlen(uname) == 0) {
        uname = node["login_id"].as<const char*>();
    }
    const char* nick = node["nickname"].as<const char*>();
    if (!nick || std::strlen(nick) == 0) {
        nick = node["display_name"].as<const char*>();
    }
    const char* sid = node["short_id"].as<const char*>();
    if (!sid || std::strlen(sid) == 0) {
        sid = node["shortId"].as<const char*>();
    }
    const char* fid = node["friend_id"].as<const char*>();
    if (!fid || std::strlen(fid) == 0) {
        fid = node["friendId"].as<const char*>();
    }
    if (!fid || std::strlen(fid) == 0) {
        fid = node["id"].as<const char*>();
    }

    c.username = uname ? uname : std::string();
    const std::string nickname = nick ? nick : std::string();
    c.display_name = nickname.empty() ? c.username : nickname;
    c.short_id = (sid && std::strlen(sid) > 0) ? sid : std::string();
    c.friend_id = fid ? fid : std::string();
    c.unread_count = parse_unread_count(node);
    c.has_unread = c.unread_count > 0;

    if (!c.short_id.empty()) {
        c.identifier = c.short_id;
    } else if (!c.friend_id.empty()) {
        c.identifier = c.friend_id;
    } else {
        c.identifier = c.username;
    }
    return c;
}

inline bool should_include_contact(const ContactEntry& c,
                                   const std::string& my_username) {
    return c.username != my_username && !c.identifier.empty();
}

}  // namespace app::contactbook
