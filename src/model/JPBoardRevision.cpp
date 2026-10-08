// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardRevision.h"

#include <cctype>

inline namespace jf {

JJson JPBoardRevision::toJson(bool content) const {
    JJson j = JJson::object();
    j["label"] = label;
    if (!made.empty()) j["made"] = made;
    if (!summary.empty()) j["summary"] = summary;
    if (!content) return j;
    JJson sources = JJson::array();
    for (const JJson& p : provenance) sources.push(p);
    j["provenance"] = sources;
    JJson ps = JJson::array();
    for (const JPPlacement& p : placements) ps.push(p.toJson());
    j["placements"] = ps;
    JJson bps = JJson::array();
    for (const JPBoardPart& p : parts) bps.push(p.toJson());
    j["parts"] = bps;
    return j;
}

JPBoardRevision JPBoardRevision::fromJson(const JJson& j) {
    JPBoardRevision r;
    if (j["label"].isString()) r.label = j["label"].str();
    if (j["made"].isString()) r.made = j["made"].str();
    if (j["summary"].isString()) r.summary = j["summary"].str();
    if (j["provenance"].isArray())
        for (const JJson& p : j["provenance"].arr()) r.provenance.push_back(p);
    if (j["placements"].isArray())
        for (const JJson& p : j["placements"].arr()) r.placements.push_back(JPPlacement::fromJson(p));
    if (j["parts"].isArray())
        for (const JJson& p : j["parts"].arr()) r.parts.push_back(JPBoardPart::fromJson(p));
    return r;
}

std::string JPBoardRevision::next(const std::string& label) {
    if (label.empty()) return "rev A";
    size_t digits = label.size();
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(label[digits - 1]))) --digits;
    if (digits < label.size() && label.size() - digits < 18)
        return label.substr(0, digits) + std::to_string(std::stoll(label.substr(digits)) + 1);
    // A letter standing alone ("rev A", "B"), not the end of a word.
    const char last = label.back();
    const bool alone = label.size() == 1 || !std::isalpha(static_cast<unsigned char>(label[label.size() - 2]));
    if (alone && ((last >= 'A' && last < 'Z') || (last >= 'a' && last < 'z')))
        return label.substr(0, label.size() - 1) + char(last + 1);
    return label + " 2";
}

} // inline namespace jf
