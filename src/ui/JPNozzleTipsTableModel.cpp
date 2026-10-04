// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPNozzleTipsTableModel.h"

#include <algorithm>

inline namespace jf {

bool JPNozzleTipsTableModel::checked(int row, int) const {
    if (!m_package) return false;
    const std::string& id = m_tips[size_t(row)].first;
    return std::find(m_package->compatibleNozzleTipIds.begin(), m_package->compatibleNozzleTipIds.end(), id)
           != m_package->compatibleNozzleTipIds.end();
}

void JPNozzleTipsTableModel::setChecked(int row, int c, bool on) {
    if (!m_package || c != 1) return;
    const std::string& id = m_tips[size_t(row)].first;
    auto& ids = m_package->compatibleNozzleTipIds;
    const auto it = std::find(ids.begin(), ids.end(), id);
    if (on && it == ids.end()) ids.push_back(id);
    if (!on && it != ids.end()) ids.erase(it);
    if (onChanged) onChanged();
}

} // inline namespace jf
