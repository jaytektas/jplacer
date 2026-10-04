// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionSettingsTableModel.h"

inline namespace jf {

JPVisionSettingsTableModel::JPVisionSettingsTableModel(JPConfiguration& config) : m_config(config) {}

JPTableModel::Column JPVisionSettingsTableModel::column(int c) const {
    return c == 0 ? Column { "Name", "", Kind::Text } : Column { "Assigned To", "", Kind::Text };
}

int JPVisionSettingsTableModel::rowCount() const { return int(m_config.visionSettings().size()); }

JPVisionSettings* JPVisionSettingsTableModel::settings(int row) const {
    auto& v = const_cast<JPConfiguration&>(m_config).visionSettings();
    return row >= 0 && size_t(row) < v.size() ? &v[size_t(row)] : nullptr;
}

int JPVisionSettingsTableModel::rowOf(const std::string& id) const {
    const auto& v = m_config.visionSettings();
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i].id == id) return int(i);
    return -1;
}

std::string JPVisionSettingsTableModel::rowKey(int row) const {
    const JPVisionSettings* v = settings(row);
    return v ? v->id : std::string();
}

bool JPVisionSettingsTableModel::rowShown(int row) const {
    const JPVisionSettings* v = settings(row);
    return v && v->kind == m_kind;
}

std::string JPVisionSettingsTableModel::text(int row, int c) const {
    const JPVisionSettings* v = settings(row);
    if (!v) return {};
    if (c == 0) return v->name;
    return usedIn ? usedIn(*v) : std::string();
}

bool JPVisionSettingsTableModel::editable(int row, int c) const {
    const JPVisionSettings* v = settings(row);
    return v && c == 0 && !v->isStock();
}

bool JPVisionSettingsTableModel::setText(int row, int c, const std::string& text, std::string&) {
    JPVisionSettings* v = settings(row);
    if (!v || c != 0 || v->isStock()) return false;
    v->name = text;
    if (onChanged) onChanged();
    return true;
}

} // inline namespace jf
