// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLcscLookup.h"

#include "common/JPWholeFile.h"
#include "common/JPlacerLog.h"

#include <j/core/Log.h>
#include <j/io/HttpClient.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

inline namespace jf {

namespace fs = std::filesystem;

bool JPLcscLookup::find(const std::string& cacheDir, const std::string& lcsc, JPEasyEdaPart& out, std::string& why) {
    const std::string number = JPEasyEdaPart::partNumber(lcsc);
    if (number.empty()) {
        why = "\"" + lcsc + "\" is not an LCSC part number (C followed by digits, as C14663)";
        return false;
    }
    const fs::path kept = fs::path(cacheDir) / (number + ".json");
    std::error_code ec;
    if (fs::exists(kept, ec)) {
        std::ifstream in(kept);
        std::stringstream text;
        text << in.rdbuf();
        if (JPEasyEdaPart::parse(text.str(), out, why)) return true;
    }
    const JHttpResponse r = JHttpClient::getSync(JPEasyEdaPart::url(number), kTimeoutMs);
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "LCSC " << number << ": EasyEDA answered " << r.status
                                             << (r.error.empty() ? "" : " (" + r.error + ")");
    if (!r.ok()) {
        why = r.error.empty() ? "EasyEDA answered " + std::to_string(r.status) : "EasyEDA could not be reached: " + r.error;
        return false;
    }
    if (!JPEasyEdaPart::parse(r.text(), out, why)) {
        why = number + ": " + why;
        return false;
    }
    std::string error;
    if (!JPWholeFile::write(kept.string(), r.text(), error))
        JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "LCSC " << number << " not kept: " << error;
    return true;
}

void JPLcscLookup::giveFootprint(JPPackage& package, const JPEasyEdaPart& part) {
    JPFootprint mm;
    mm.units = JPLengthUnit::Millimeters;
    mm.pads = part.pads;
    package.footprint.pads = mm.inUnits(package.footprint.units).pads;
}

void JPLcscLookup::describe(JPPart& part, const JPEasyEdaPart& from) {
    if (!part.name && !from.description.empty()) part.name = from.description;
    if (!from.mpn.empty() && std::none_of(part.identifiers.begin(), part.identifiers.end(),
                                          [](const JPPart::Identifier& i) { return i.kind == "mpn"; }))
        part.identifiers.push_back({ "mpn", from.manufacturer, from.mpn });
    if (part.height.value() <= 0 && from.heightMm && *from.heightMm > 0)
        part.height = JPLength(*from.heightMm, JPLengthUnit::Millimeters);
}

std::string JPLcscLookup::lcscOf(const JPPart& part) {
    for (const JPPart::Identifier& i : part.identifiers) {
        if (i.kind != "supplierPn") continue;
        std::string org = i.org;
        std::transform(org.begin(), org.end(), org.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        const std::string n = JPEasyEdaPart::partNumber(i.code);
        if (!n.empty() && (org.find("LCSC") != std::string::npos || org.find("JLC") != std::string::npos || i.code[0] == 'C'
                           || i.code[0] == 'c'))
            return n;
    }
    return "";
}

} // inline namespace jf
