#pragma once

namespace app::contactbook {

enum class SelectionKind {
    Contact,
    AddFriend,
    Pending,
    Invalid,
};

inline SelectionKind resolve_selection_kind(int select_index, int contact_count) {
    if (select_index < 0) return SelectionKind::Invalid;
    if (select_index < contact_count) return SelectionKind::Contact;
    if (select_index == contact_count) return SelectionKind::AddFriend;
    if (select_index == contact_count + 1) return SelectionKind::Pending;
    return SelectionKind::Invalid;
}

}  // namespace app::contactbook
