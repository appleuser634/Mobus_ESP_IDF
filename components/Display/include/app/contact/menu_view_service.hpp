#pragma once

#include <vector>

#include "app/contact/domain.hpp"
#include "ui/contact/book_mvp.hpp"

namespace app::contactbookview {

inline std::vector<ui::contactbook::RowData> build_rows(
    const std::vector<app::contactbook::ContactEntry>& contacts) {
    std::vector<ui::contactbook::RowData> rows;
    rows.reserve(contacts.size() + 2);
    for (const auto& contact : contacts) {
        ui::contactbook::RowData row;
        row.label = contact.display_name;
        row.has_unread = contact.has_unread;
        row.unread_count = contact.unread_count;
        rows.push_back(row);
    }
    rows.push_back({"+ Add Friend", false, 0});
    rows.push_back({"Pending Requests", false, 0});
    return rows;
}

inline void mark_read(app::contactbook::ContactEntry& contact) {
    contact.unread_count = 0;
    contact.has_unread = false;
}

}  // namespace app::contactbookview
