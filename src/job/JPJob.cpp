// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPJob.h"

#include "common/JPWholeFile.h"

#include <array>

inline namespace jf {

namespace {

constexpr int kVersion = 1;

const char* sideName(JPPlacement::Side s) { return s == JPPlacement::Side::Bottom ? "bottom" : "top"; }

const char* mountingName(JPPlacement::Mounting m) {
    switch (m) {
        case JPPlacement::Mounting::Smd:         return "smd";
        case JPPlacement::Mounting::ThroughHole: return "tht";
        case JPPlacement::Mounting::Unknown:     break;
    }
    return "";
}

JPPlacement::Mounting mountingOf(const std::string& s) {
    if (s == "smd") return JPPlacement::Mounting::Smd;
    if (s == "tht") return JPPlacement::Mounting::ThroughHole;
    return JPPlacement::Mounting::Unknown;
}

// The text fields of a placement, by the name each is kept under.
template <typename P>
auto textFields(P& p) {
    return std::array<std::pair<const char*, decltype(&p.designator)>, 16> { {
        { "designator", &p.designator }, { "mpn", &p.mpn }, { "manufacturer", &p.manufacturer },
        { "value", &p.value }, { "tolerance", &p.tolerance }, { "voltage", &p.voltage }, { "power", &p.power },
        { "dielectric", &p.dielectric }, { "temperature", &p.temperature }, { "footprint", &p.footprint },
        { "supplierPackage", &p.supplierPackage }, { "description", &p.description }, { "device", &p.device },
        { "datasheet", &p.datasheet }, { "cadId", &p.cadId }, { "partId", &p.partId },
    } };
}

JJson placementJson(const JPPlacement& p) {
    JJson j = JJson::object();
    for (const auto& [key, field] : textFields(p))
        if (!field->empty()) j[key] = *field;
    j["x"]        = p.x;
    j["y"]        = p.y;
    j["rotation"] = p.rotationDeg;
    j["side"]     = sideName(p.side);
    if (p.fiducial) {
        j["fiducial"]   = true;
        j["fiducialMm"] = p.fiducialMm;
    }
    if (p.hasPin1) {
        j["pin1X"] = p.pin1X;
        j["pin1Y"] = p.pin1Y;
    }
    if (!p.supplierNumbers.empty()) {
        JJson ns = JJson::array();
        for (const JPSupplierNumber& n : p.supplierNumbers) {
            JJson o = JJson::object();
            o["supplier"] = n.supplier;
            o["number"]   = n.number;
            ns.push(std::move(o));
        }
        j["supplierNumbers"] = std::move(ns);
    }
    if (p.mounting != JPPlacement::Mounting::Unknown) j["mounting"] = mountingName(p.mounting);
    if (p.doNotPlace) j["doNotPlace"] = true;
    if (p.pins > 0) j["pins"] = p.pins;
    if (p.partGuessed) j["partGuessed"] = true;
    if (p.rotationSet) j["rotationSet"] = p.rotationSetDeg;
    if (!p.other.empty()) {
        JJson o = JJson::object();
        for (const auto& [heading, text] : p.other) o[heading] = text;
        j["other"] = std::move(o);
    }
    return j;
}

JPPlacement placementOf(const JJson& j) {
    JPPlacement p;
    for (const auto& [key, field] : textFields(p)) *field = j[key].str();
    p.x           = j["x"].number();
    p.y           = j["y"].number();
    p.rotationDeg = j["rotation"].number();
    p.side        = j["side"].str() == "bottom" ? JPPlacement::Side::Bottom : JPPlacement::Side::Top;
    p.fiducial    = j["fiducial"].boolean();
    p.fiducialMm  = j["fiducialMm"].number();
    p.hasPin1     = j.contains("pin1X");
    p.pin1X       = j["pin1X"].number();
    p.pin1Y       = j["pin1Y"].number();
    for (const JJson& n : j["supplierNumbers"].arr()) p.supplierNumbers.push_back({ n["supplier"].str(), n["number"].str() });
    p.mounting    = mountingOf(j["mounting"].str());
    p.doNotPlace  = j["doNotPlace"].boolean();
    p.pins        = int(j["pins"].number());
    p.partGuessed = j["partGuessed"].boolean();
    p.rotationSet = j.contains("rotationSet");
    p.rotationSetDeg = j["rotationSet"].number();
    for (const auto& [heading, text] : j["other"].obj()) p.other.emplace_back(heading, text.str());
    return p;
}

} // namespace

JJson JPJob::toJson() const {
    JJson j = JJson::object();
    j["version"]    = kVersion;
    j["name"]       = board.name;
    JJson ss = JJson::array();
    for (const JPSource& src : sources) {
        JJson o = JJson::object();
        o["kind"]        = src.kind == JPSource::Kind::Bom ? "bom" : "placements";
        o["path"]        = src.path;
        o["tool"]        = JPCadTool::key(src.tool);
        o["fingerprint"] = src.fingerprint;
        ss.push(std::move(o));
    }
    j["sources"] = std::move(ss);
    JJson f = JJson::object();
    f["originX"] = frame.originX;
    f["originY"] = frame.originY;
    f["width"]   = frame.width;
    f["height"]  = frame.height;
    j["frame"] = std::move(f);
    JJson bc = JJson::array();
    for (const std::string& c : bomChoices) bc.push(c);
    j["bomChoices"] = std::move(bc);
    JJson ps = JJson::array();
    for (const JPPlacement& p : board.placements) ps.push(placementJson(p));
    j["placements"] = std::move(ps);
    j["parts"]      = parts.toJson();
    return j;
}

bool JPJob::fromJson(const JJson& j, JPJob& out, std::string& error) {
    if (!j.isObject()) {
        error = "not a JSON object";
        return false;
    }
    if (const int v = int(j["version"].number()); v != kVersion) {
        error = "written by another version of jplacer (format " + std::to_string(v) + ")";
        return false;
    }
    JPJob job;
    job.board.name = j["name"].str();
    for (const JJson& o : j["sources"].arr())
        job.sources.push_back({ o["kind"].str() == "bom" ? JPSource::Kind::Bom : JPSource::Kind::Placements, o["path"].str(),
                                JPCadTool::fromKey(o["tool"].str()), o["fingerprint"].str() });
    job.frame.originX = j["frame"]["originX"].number();
    job.frame.originY = j["frame"]["originY"].number();
    job.frame.width   = j["frame"]["width"].number();
    job.frame.height  = j["frame"]["height"].number();
    for (const JJson& c : j["bomChoices"].arr()) job.bomChoices.push_back(c.str());
    for (const JJson& p : j["placements"].arr()) job.board.placements.push_back(placementOf(p));
    if (!JPPartsStore::fromJson(j["parts"], job.parts, error)) {
        error = "its parts: " + error;
        return false;
    }
    out = std::move(job);
    return true;
}

bool JPJob::save(const std::string& path, std::string& error) const {
    return JPWholeFile::write(path, toJson().dump(2) + "\n", error);
}

bool JPJob::load(const std::string& path, JPJob& out, std::string& error) {
    const std::optional<JJson> doc = JJson::tryParseFile(path);
    if (!doc) {
        error = path + ": cannot be read, or is not valid JSON";
        return false;
    }
    if (!fromJson(*doc, out, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

} // inline namespace jf
