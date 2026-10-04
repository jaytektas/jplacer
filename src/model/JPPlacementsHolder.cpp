// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementsHolder.h"

inline namespace jf {

JPProfile JPPlacementsHolder::outline() const {
    if (profile) return *profile;
    return JPProfile::rectangle(dimensions.x(), dimensions.y(), dimensions.units());
}

JPPlacement* JPPlacementsHolder::find(const std::string& id) {
    if (id.empty()) return nullptr;
    for (JPPlacement& p : placements)
        if (p.id == id) return &p;
    return nullptr;
}

const JPPlacement* JPPlacementsHolder::find(const std::string& id) const {
    return const_cast<JPPlacementsHolder*>(this)->find(id);
}

} // inline namespace jf
