// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLcscLookup.h"

#include "JPEasyEdaModel.h"

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

bool JPLcscLookup::find(const std::string& cacheDir, const std::string& lcsc, JPEasyEdaPart& out, std::string& why, bool* wasKept) {
    if (wasKept) *wasKept = false;
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
        if (JPEasyEdaPart::parse(text.str(), out, why)) {
            if (wasKept) *wasKept = true;
            return true;
        }
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

bool JPLcscLookup::modelHeight(const std::string& cacheDir, const JPEasyEdaPart& part, double& heightMm, std::string& why,
                               bool* wasKept) {
    if (wasKept) *wasKept = false;
    if (!part.model) {
        why = "EasyEDA has no 3D model for it";
        return false;
    }
    const JPEasyEdaPart::Model& m = *part.model;
    const fs::path kept = fs::path(cacheDir) / kModelFolder / (m.uuid + ".obj");
    std::error_code ec;
    if (fs::exists(kept, ec)) {
        std::ifstream in(kept);
        std::stringstream text;
        text << in.rdbuf();
        if (JPEasyEdaModel::height(text.str(), m.rotationX, m.rotationY, heightMm, why)) {
            if (wasKept) *wasKept = true;
            return true;
        }
    }
    const JHttpResponse r = JHttpClient::getSync(JPEasyEdaPart::modelUrl(m.uuid), kTimeoutMs);
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "LCSC " << part.lcsc << " 3D model " << m.uuid << ": EasyEDA answered "
                                             << r.status << (r.error.empty() ? "" : " (" + r.error + ")");
    if (!r.ok()) {
        why = r.error.empty() ? "its 3D model: EasyEDA answered " + std::to_string(r.status)
                              : "its 3D model: EasyEDA could not be reached: " + r.error;
        return false;
    }
    if (!JPEasyEdaModel::height(r.text(), m.rotationX, m.rotationY, heightMm, why)) return false;
    fs::create_directories(kept.parent_path(), ec);
    std::string error;
    if (!JPWholeFile::write(kept.string(), r.text(), error))
        JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "LCSC " << part.lcsc << " 3D model not kept: " << error;
    return true;
}

bool JPLcscLookup::giveDimensions(JPPackage& package, const JPEasyEdaPart& part, std::optional<double> heightMm) {
    bool given = false;
    JPFootprint& f = package.footprint;
    if (part.model && part.model->widthMm > 0 && part.model->lengthMm > 0 && f.bodyWidth <= 0 && f.bodyHeight <= 0) {
        const double perMm = JPLength(1, JPLengthUnit::Millimeters).convertToUnits(f.units).value();
        f.bodyWidth = part.model->widthMm * perMm;
        f.bodyHeight = part.model->lengthMm * perMm;
        package.bodySource = modelSource(part);
        given = true;
    }
    if (heightMm && *heightMm > 0 && package.height.value() <= 0) {
        package.height = JPLength(*heightMm, JPLengthUnit::Millimeters);
        package.heightSource = modelSource(part);
        given = true;
    }
    return given;
}

void JPLcscLookup::giveFootprint(JPPackage& package, const JPEasyEdaPart& part) {
    JPFootprint mm;
    mm.units = JPLengthUnit::Millimeters;
    mm.pads = part.pads;
    package.footprint.pads = mm.inUnits(package.footprint.units).pads;
    package.padsSource = source(part);
}

std::string JPLcscLookup::source(const JPEasyEdaPart& part) { return "LCSC " + part.lcsc + " (EasyEDA)"; }

std::string JPLcscLookup::modelSource(const JPEasyEdaPart& part) {
    return "LCSC " + part.lcsc + "'s 3D model (EasyEDA)" + (part.model ? " " + part.model->uuid : std::string());
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
