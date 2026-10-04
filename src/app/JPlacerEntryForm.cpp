// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerEntryForm.h"

#include "library/JPEntryFields.h"
#include "library/JPFootprintMaker.h"

#include <cstdlib>
#include <sstream>

inline namespace jf {

std::vector<JPFormPage::Field> JPlacerEntryForm::fields(JPEntry::Kind kind) {
    std::vector<JPFormPage::Field> out;
    for (const JPEntryFields::Field& f : JPEntryFields::of(kind))
        out.push_back({ f.key, f.label, f.key == "package" || f.key == "footprint" });
    return out;
}

void JPlacerEntryForm::fill(JPFormPage& page, const JPPartsStore& store, const JPEntry& e) {
    for (const JPEntryFields::Field& f : JPEntryFields::of(e.kind))
        page.setValue(f.key, JPEntryFields::get(store, e, f.key));
    // Pads are counted, not typed.
    page.setFieldEditable("pads", false);
}

const char* JPlacerEntryForm::prompt(bool quad) {
    return quad ? "Pins on each side, pitch, pad centres across, pad length, pad width, exposed pad (0 for none), in mm:"
                : "Pins, pitch, pad centres across, pad length, pad width, in mm:";
}

bool JPlacerEntryForm::make(bool quad, const std::string& numbers, const std::string& name, JPFootprint& out,
                            std::string& error) {
    std::vector<double> v;
    std::stringstream ss(numbers);
    std::string item;
    while (std::getline(ss, item, ',')) {
        char* end = nullptr;
        const double d = std::strtod(item.c_str(), &end);
        if (end == item.c_str()) {
            error = "'" + item + "' is not a number";
            return false;
        }
        v.push_back(d);
    }
    const size_t want = quad ? 6 : 5;
    if (v.size() != want) {
        error = "give " + std::to_string(want) + " numbers, separated by commas";
        return false;
    }
    for (size_t i = 0; i + (quad ? 1 : 0) < v.size(); ++i)
        if (v[i] <= 0) {
            error = "every number but the exposed pad must be more than 0";
            return false;
        }
    if (quad) {
        JPFootprintMaker::Quad q { int(v[0]), v[1], v[2], v[3], v[4], v[2] - v[3], v[5] };
        out = JPFootprintMaker::quad(name, q);
    } else {
        if (int(v[0]) % 2 != 0) {
            error = "a dual footprint has an even number of pins";
            return false;
        }
        JPFootprintMaker::Dual d { int(v[0]), v[1], v[2], v[3], v[4], v[2] - v[3], (v[0] / 2) * v[1] };
        out = JPFootprintMaker::dual(name, d);
    }
    return true;
}

} // inline namespace jf
