// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartsTableModel.h"

#include "JPLengthCell.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

enum Col { kId, kDescription, kHeight, kDepth, kPackage, kSpeed, kBottomVision, kFiducialVision, kPlacements, kFeeders, kColumns };

// OpenPnP's PercentConverter.
std::string percent(double d) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f%%", d * 100);
    return buf;
}

} // namespace

JPPartsTableModel::JPPartsTableModel(JPConfiguration& config) : m_config(config) {}

int JPPartsTableModel::columnCount() const { return kColumns; }

JPTableModel::Column JPPartsTableModel::column(int c) const {
    switch (c) {
        case kId:             return { "ID", "", Kind::Text };
        case kDescription:    return { "Description", "", Kind::Text };
        case kHeight:         return { "Height", "Part height; the distance between board surface and pick position.", Kind::Text };
        case kDepth:
            return { "Through-Board Depth",
                     "The length of any through-board features on the part. That is, the size any feature which is "
                     "below the board surface when placed. This is added to the Part Height to determine how high the "
                     "nozzle needs to be lifted when processing Dynamic Safe Z.\nThis should be zero for purely "
                     "surface-mount parts.",
                     Kind::Text };
        case kPackage:        return { "Package", "", Kind::Choice };
        case kSpeed:          return { "Speed %", "", Kind::Text };
        case kBottomVision:   return { "BottomVision", "", Kind::Choice };
        case kFiducialVision: return { "FiducialVision", "", Kind::Choice };
        case kPlacements:     return { "Placements", "", Kind::Number };
        case kFeeders:        return { "Feeders", "", Kind::Number };
    }
    return {};
}

int JPPartsTableModel::rowCount() const { return int(m_config.parts().size()); }

JPPart* JPPartsTableModel::part(int row) const {
    return row >= 0 && size_t(row) < m_config.parts().size() ? m_config.parts()[size_t(row)].get() : nullptr;
}

int JPPartsTableModel::rowOf(const JPPart* p) const {
    for (size_t i = 0; i < m_config.parts().size(); ++i)
        if (m_config.parts()[i].get() == p) return int(i);
    return -1;
}

std::string JPPartsTableModel::rowKey(int row) const {
    const JPPart* p = part(row);
    return p ? p->id : std::string();
}

std::string JPPartsTableModel::text(int row, int c) const {
    const JPPart* p = part(row);
    if (!p) return {};
    switch (c) {
        case kId:          return p->id;
        case kDescription: return p->name.value_or("");
        case kHeight:      return JPLengthCell::text(p->height, true);
        case kDepth:       return JPLengthCell::text(p->throughBoardDepth, true);
        case kPackage:     return m_config.package(p->packageId) ? m_config.package(p->packageId)->id : std::string();
        case kSpeed:       return percent(p->speed);
        case kBottomVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->bottomVisionId);
            return v ? v->name : std::string();
        }
        case kFiducialVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->fiducialVisionId);
            return v ? v->name : std::string();
        }
        case kPlacements: return std::to_string(m_config.placementCount(p->id));
        case kFeeders:    return "0";   // the machine has no feeders yet
    }
    return {};
}

double JPPartsTableModel::number(int row, int c) const {
    if (c == kPlacements) {
        const JPPart* p = part(row);
        return p ? m_config.placementCount(p->id) : 0;
    }
    return 0;
}

bool JPPartsTableModel::editable(int, int c) const {
    return c >= kDescription && c <= kFiducialVision;
}

std::vector<const JPPackage*> JPPartsTableModel::packageChoices() const {
    std::vector<const JPPackage*> out;
    for (const auto& k : m_config.packages()) out.push_back(k.get());
    std::sort(out.begin(), out.end(), [](const JPPackage* a, const JPPackage* b) { return a->id < b->id; });
    return out;
}

std::vector<const JPVisionSettings*> JPPartsTableModel::visionChoices(JPVisionSettings::Kind kind) const {
    std::vector<const JPVisionSettings*> out;
    for (const JPVisionSettings& v : m_config.visionSettings())
        if (v.kind == kind) out.push_back(&v);
    std::sort(out.begin(), out.end(), [](const JPVisionSettings* a, const JPVisionSettings* b) { return a->name < b->name; });
    out.push_back(nullptr);
    return out;
}

std::vector<std::string> JPPartsTableModel::choices(int, int c) const {
    std::vector<std::string> out;
    if (c == kPackage)
        for (const JPPackage* k : packageChoices()) out.push_back(k->id);
    if (c == kBottomVision || c == kFiducialVision)
        for (const JPVisionSettings* v :
             visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial))
            out.push_back(v ? v->name : std::string());
    return out;
}

bool JPPartsTableModel::setText(int row, int c, const std::string& text, std::string& error) {
    JPPart* p = part(row);
    if (!p) return false;
    switch (c) {
        case kDescription:
            p->name = text;
            break;
        case kHeight:
        case kDepth: {
            JPLength& l = c == kHeight ? p->height : p->throughBoardDepth;
            if (!JPLengthCell::parse(text, l, l)) {
                error = "'" + text + "' is not a length";
                return false;
            }
            break;
        }
        case kSpeed: {
            std::string t;
            for (const char ch : text)
                if (ch != '%') t += ch;
            char* end = nullptr;
            const double v = std::strtod(t.c_str(), &end);
            if (end == t.c_str()) {
                error = "'" + text + "' is not a percentage";
                return false;
            }
            p->speed = v * 0.01;
            break;
        }
        default: return false;
    }
    if (onChanged) onChanged();
    return true;
}

void JPPartsTableModel::setChoice(int row, int c, int index) {
    JPPart* p = part(row);
    if (!p || index < 0) return;
    if (c == kPackage) {
        const auto ks = packageChoices();
        if (size_t(index) < ks.size()) p->packageId = ks[size_t(index)]->id;
    } else if (c == kBottomVision || c == kFiducialVision) {
        const auto vs = visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial);
        if (size_t(index) >= vs.size()) return;
        (c == kBottomVision ? p->bottomVisionId : p->fiducialVisionId) = vs[size_t(index)] ? vs[size_t(index)]->id : std::string();
    } else {
        return;
    }
    if (onChanged) onChanged();
}

} // inline namespace jf
