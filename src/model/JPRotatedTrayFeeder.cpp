// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRotatedTrayFeeder.h"

#include "JPLocationJson.h"
#include "JPLocationXml.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

int JPRotatedTrayFeeder::capacity() const { return std::max(cols, 1) * std::max(rows, 1); }

JPLocation JPRotatedTrayFeeder::pickLocation(const JPLocation& a) const {
    const int c = std::max(cols, 1), r = std::max(rows, 1);
    int base0 = feedCount - 1;
    if (feedCount == 0) base0 = 0;
    else if (feedCount > c * r) base0 = c * r - 1;   // past the end: the last
    // Along a row, then the next (an older file part way through: along a column).
    int col, row;
    if (legacyPickingInProgress && c >= r) {
        row = base0 % r;
        col = base0 / r;
    } else {
        row = base0 / c;
        col = base0 % c;
    }
    // Rows go the tray's negative Y; the steps turned with the tray.
    const JPLocation step = offsets.convertToUnits(a.units());
    const double angle = a.rotation() * M_PI / 180;
    const double dx = step.x() * col, dy = -step.y() * row;
    return JPLocation(a.units(), a.x() + dx * std::cos(angle) - dy * std::sin(angle), a.y() + dx * std::sin(angle) + dy * std::cos(angle),
                      a.z(), a.rotation() + componentRotation);
}

bool JPRotatedTrayFeeder::calculateOffsets(const JPLocation& at, double& rotation, std::string& why) {
    constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
    if (cols < 1 || rows < 1) {
        why = "The tray must have at least one row and one column.";
        return false;
    }
    const JPLocation a = at.convertToUnits(kMm), b = firstRowLast.convertToUnits(kMm), c = last.convertToUnits(kMm);
    const double ab = std::hypot(b.x() - a.x(), b.y() - a.y()), bc = std::hypot(c.x() - b.x(), c.y() - b.y());
    char buf[300];
    if (ab > 0 && cols == 1) {
        why = "Points A and B are different which is inconsistent with a single tray column. Either set points A and B "
              "to be the same or increase the number of columns.";
        return false;
    }
    if (ab == 0 && cols > 1) {
        std::snprintf(buf, sizeof buf, "Points A and B are the same which is inconsistent with %d columns. Either set "
                                       "points A and B to be different or set the number of columns to 1.", cols);
        why = buf;
        return false;
    }
    if (bc > 0 && rows == 1) {
        why = "Points B and C are different which is inconsistent with a single tray row. Either set points B and C to "
              "be the same or increase the number of rows.";
        return false;
    }
    if (bc == 0 && rows > 1) {
        std::snprintf(buf, sizeof buf, "Points B and C are the same which is inconsistent with %d rows. Either set "
                                       "points B and C to be different or set the number of rows to 1.", rows);
        why = buf;
        return false;
    }
    double colStep = cols > 1 ? ab / (cols - 1) : 0, rowStep = rows > 1 ? bc / (rows - 1) : 0;
    const double rowAngle = a.angleTo(b);
    const double colAngle = b.angleTo(c);
    if (rows > 1 && cols > 1) {
        const double check = std::remainder(rowAngle - colAngle, 360.0);
        if (std::abs(check) < 90 - 2.5 || std::abs(check) > 90 + 2.5) {
            std::snprintf(buf, sizeof buf, "Tray angle ABC should be 90 degrees but is %.3f degrees, double check "
                                           "coordinates of points A, B and C.", std::abs(check));
            why = buf;
            return false;
        }
        // Defined the other way round: the rows step the other way.
        if (check < 0) rowStep = -rowStep;
    }
    rotation = at.rotation();
    if (cols > 1) rotation = rowAngle;
    else if (rows > 1) rotation = colAngle + 90;
    offsets = JPLocation(kMm, colStep, rowStep, 0, 0);
    return true;
}

JPRotatedTrayFeeder JPRotatedTrayFeeder::fromXml(const JPXmlNode& own) {
    JPRotatedTrayFeeder t;
    auto text = [&own](const char* k) {
        const std::string* v = own.get(k);
        return v ? *v : std::string();
    };
    auto integer = [&text](const char* k, int def) {
        const std::string v = text(k);
        return v.empty() ? def : std::atoi(v.c_str());
    };
    t.cols = integer("tray-count-cols", 1);
    t.rows = integer("tray-count-rows", 1);
    t.feedCount = integer("feed-count", 0);
    if (const std::string v = text("component-rotation-in-tray"); !v.empty()) t.componentRotation = std::strtod(v.c_str(), nullptr);
    t.legacyPickingInProgress = text("legacy-picking-in-progress") == "true";
    if (const JPXmlNode* o = own.child("offsets")) t.offsets = JPLocationXml::from(*o);
    if (const JPXmlNode* b = own.child("first-row-last-component-location")) t.firstRowLast = JPLocationXml::from(*b);
    if (const JPXmlNode* c = own.child("last-component-location")) t.last = JPLocationXml::from(*c);
    return t;
}

JPRotatedTrayFeeder JPRotatedTrayFeeder::fromJson(const JJson& j) {
    JPRotatedTrayFeeder t;
    t.cols = int(j["cols"].number(1));
    t.rows = int(j["rows"].number(1));
    t.offsets = JPLocationJson::from(j["offsets"]);
    t.feedCount = int(j["feedCount"].number(0));
    t.componentRotation = j["componentRotation"].number(0.0);
    t.legacyPickingInProgress = j["legacyPickingInProgress"].boolean(false);
    t.firstRowLast = JPLocationJson::from(j["firstRowLast"]);
    t.last = JPLocationJson::from(j["last"]);
    return t;
}

JJson JPRotatedTrayFeeder::toJson() const {
    JJson j = JJson::object();
    j["cols"] = cols;
    j["rows"] = rows;
    j["offsets"] = JPLocationJson::to(offsets);
    j["feedCount"] = feedCount;
    j["componentRotation"] = componentRotation;
    j["legacyPickingInProgress"] = legacyPickingInProgress;
    j["firstRowLast"] = JPLocationJson::to(firstRowLast);
    j["last"] = JPLocationJson::to(last);
    return j;
}

} // inline namespace jf
