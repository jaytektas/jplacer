// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPackagesTableModel.h"

#include <algorithm>

inline namespace jf {

namespace {
enum Col { kId, kDescription, kTape, kBottomVision, kFiducialVision };
}

JPPackagesTableModel::JPPackagesTableModel(JPConfiguration& config) : m_config(config) {}

JPTableModel::Column JPPackagesTableModel::column(int c) const {
    switch (c) {
        case kId:          return { "ID", "", Kind::Text };
        case kDescription: return { "Description", "", Kind::Text };
        case kTape:
            return { "Tape Specification",
                     "This is a text field used by some feeder implementations. Check your feeder documentation for "
                     "more details.",
                     Kind::Text };
        case kBottomVision:   return { "BottomVision", "", Kind::Choice };
        case kFiducialVision: return { "FiducialVision", "", Kind::Choice };
    }
    return {};
}

JPPackage* JPPackagesTableModel::package(int row) const {
    return row >= 0 && size_t(row) < m_config.packages().size() ? m_config.packages()[size_t(row)].get() : nullptr;
}

int JPPackagesTableModel::rowOf(const JPPackage* p) const {
    for (size_t i = 0; i < m_config.packages().size(); ++i)
        if (m_config.packages()[i].get() == p) return int(i);
    return -1;
}

std::string JPPackagesTableModel::rowKey(int row) const {
    const JPPackage* p = package(row);
    return p ? p->id : std::string();
}

std::string JPPackagesTableModel::text(int row, int c) const {
    const JPPackage* p = package(row);
    if (!p) return {};
    switch (c) {
        case kId:          return p->id;
        case kDescription: return p->description.value_or("");
        case kTape:        return p->tapeSpecification.value_or("");
        case kBottomVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->bottomVisionId);
            return v ? v->name : std::string();
        }
        case kFiducialVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->fiducialVisionId);
            return v ? v->name : std::string();
        }
    }
    return {};
}

std::vector<const JPVisionSettings*> JPPackagesTableModel::visionChoices(JPVisionSettings::Kind kind) const {
    std::vector<const JPVisionSettings*> out;
    for (const JPVisionSettings& v : m_config.visionSettings())
        if (v.kind == kind) out.push_back(&v);
    std::sort(out.begin(), out.end(), [](const JPVisionSettings* a, const JPVisionSettings* b) { return a->name < b->name; });
    out.push_back(nullptr);
    return out;
}

std::vector<std::string> JPPackagesTableModel::choices(int, int c) const {
    std::vector<std::string> out;
    if (c == kBottomVision || c == kFiducialVision)
        for (const JPVisionSettings* v :
             visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial))
            out.push_back(v ? v->name : std::string());
    return out;
}

bool JPPackagesTableModel::setText(int row, int c, const std::string& text, std::string&) {
    JPPackage* p = package(row);
    if (!p) return false;
    if (c == kDescription) p->description = text;
    else if (c == kTape) p->tapeSpecification = text;
    else return false;
    if (onChanged) onChanged();
    return true;
}

void JPPackagesTableModel::setChoice(int row, int c, int index) {
    JPPackage* p = package(row);
    if (!p || (c != kBottomVision && c != kFiducialVision)) return;
    const auto vs = visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial);
    if (index < 0 || size_t(index) >= vs.size()) return;
    (c == kBottomVision ? p->bottomVisionId : p->fiducialVisionId) = vs[size_t(index)] ? vs[size_t(index)]->id : std::string();
    if (onChanged) onChanged();
}

} // inline namespace jf
