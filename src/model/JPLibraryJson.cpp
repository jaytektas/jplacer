// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLibraryJson.h"

#include "openpnp/JPXmlJson.h"

#include <cstdint>
#include <cstdio>

inline namespace jf {

namespace {

// FNV-1a, 64 bits: the same text gives the same number on every machine and every run.
uint64_t fnv(const std::string& s, uint64_t h = 1469598103934665603ull) {
    for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

JJson JPLibraryJson::part(const JPPart& p) {
    JJson o = JJson::object();
    o["id"] = p.id;
    o["uuid"] = p.uuid;
    if (!p.value.empty()) o["value"] = p.value;
    if (!p.datasheet.empty()) o["datasheet"] = p.datasheet;
    JJson ids = JJson::array();
    for (const auto& i : p.identifiers) {
        JJson x = JJson::object();
        x["kind"] = i.kind;
        x["org"] = i.org;
        x["code"] = i.code;
        ids.push(x);
    }
    o["identifiers"] = ids;
    JJson akas = JJson::array();
    for (const auto& a : p.akas) {
        JJson x = JJson::object();
        x["field"] = a.field;
        x["text"] = a.text;
        x["learnedFrom"] = a.learnedFrom;
        x["when"] = a.when;
        akas.push(x);
    }
    o["akas"] = akas;
    JJson packagings = JJson::array();
    for (const auto& k : p.packagings) packagings.push(packaging(k));
    o["packagings"] = packagings;
    JJson offers = JJson::array();
    for (const auto& f : p.offers) {
        JJson x = JJson::object();
        x["supplier"] = f.supplier;
        x["sku"] = f.sku;
        x["packaging"] = f.packaging;
        x["moq"] = f.moq;
        x["priceBreaks"] = f.priceBreaks;
        x["link"] = f.link;
        x["lastPrice"] = f.lastPrice;
        x["lastWhen"] = f.lastWhen;
        offers.push(x);
    }
    o["offers"] = offers;
    o["openpnp"] = JPXmlJson::from(p.toXml());
    return o;
}

JJson JPLibraryJson::packaging(const JPPart::Packaging& k) {
    JJson x = JJson::object();
    x["kind"] = k.kind;
    x["tapeWidthMm"] = k.tapeWidthMm;
    x["pitchMm"] = k.pitchMm;
    x["tapeType"] = k.tapeType;
    x["rotationDeg"] = k.rotationDeg;
    x["quantity"] = k.quantity;
    x["note"] = k.note;
    return x;
}

JPPart JPLibraryJson::part(const JJson& j) {
    JPPart p = JPPart::fromXml(JPXmlJson::element(j["openpnp"]));
    auto str = [&j](const char* k) { return j[k].isString() ? j[k].str() : std::string(); };
    if (!str("id").empty()) p.id = str("id");
    p.uuid = str("uuid");
    p.value = str("value");
    p.datasheet = str("datasheet");
    if (j["identifiers"].isArray())
        for (const JJson& x : j["identifiers"].arr())
            p.identifiers.push_back({ x["kind"].isString() ? x["kind"].str() : "", x["org"].isString() ? x["org"].str() : "",
                                      x["code"].isString() ? x["code"].str() : "" });
    if (j["akas"].isArray())
        for (const JJson& x : j["akas"].arr())
            p.akas.push_back({ x["field"].isString() ? x["field"].str() : "", x["text"].isString() ? x["text"].str() : "",
                               x["learnedFrom"].isString() ? x["learnedFrom"].str() : "",
                               x["when"].isString() ? x["when"].str() : "" });
    auto s = [](const JJson& x, const char* k) { return x[k].isString() ? x[k].str() : std::string(); };
    if (j["packagings"].isArray())
        for (const JJson& x : j["packagings"].arr())
            p.packagings.push_back({ s(x, "kind"), x["tapeWidthMm"].number(8), x["pitchMm"].number(4), s(x, "tapeType"),
                                     x["rotationDeg"].number(), int(x["quantity"].number()), s(x, "note") });
    if (j["offers"].isArray())
        for (const JJson& x : j["offers"].arr())
            p.offers.push_back({ s(x, "supplier"), s(x, "sku"), s(x, "packaging"), int(x["moq"].number()), s(x, "priceBreaks"),
                                 s(x, "link"), s(x, "lastPrice"), s(x, "lastWhen") });
    return p;
}

JJson JPLibraryJson::package(const JPPackage& k) {
    JJson o = JJson::object();
    o["id"] = k.id;
    o["uuid"] = k.uuid;
    o["openpnp"] = JPXmlJson::from(k.toXml());
    return o;
}

JJson JPLibraryJson::footprint(const JPLibraryFootprint& f) {
    JJson o = JJson::object();
    o["uuid"] = f.uuid;
    o["name"] = f.name;
    o["packageId"] = f.packageId;
    JJson names = JJson::array();
    for (const std::string& n : f.cadNames) names.push(JJson(n));
    o["cadNames"] = names;
    o["zeroRotationDeg"] = f.zeroRotationDeg;
    if (!f.source.empty()) o["source"] = f.source;
    o["geometry"] = JPXmlJson::from(f.geometry.toXml());
    return o;
}

JPLibraryFootprint JPLibraryJson::footprint(const JJson& j) {
    JPLibraryFootprint f;
    auto s = [&j](const char* k) { return j[k].isString() ? j[k].str() : std::string(); };
    f.uuid = s("uuid");
    f.name = s("name");
    f.packageId = s("packageId");
    if (j["cadNames"].isArray())
        for (const JJson& n : j["cadNames"].arr())
            if (n.isString()) f.cadNames.push_back(n.str());
    f.zeroRotationDeg = j["zeroRotationDeg"].number();
    f.source = s("source");
    if (j["geometry"].isObject()) f.geometry = JPFootprint::fromXml(JPXmlJson::element(j["geometry"]));
    return f;
}

JPPackage JPLibraryJson::package(const JJson& j) {
    JPPackage k = JPPackage::fromXml(JPXmlJson::element(j["openpnp"]));
    if (j["id"].isString()) k.id = j["id"].str();
    if (j["uuid"].isString()) k.uuid = j["uuid"].str();
    return k;
}

std::string JPLibraryJson::fingerprint(const JPPart& p, const JPPackage* k, const JPLibraryFootprint* f) {
    // What a job places it by: its OpenPnP fields (height, package, speed, vision…), its value, its package's.
    uint64_t h = fnv(JPXmlJson::from(p.toXml()).dump());
    h = fnv("|" + p.value, h);
    for (const auto& k : p.packagings) h = fnv("|" + packaging(k).dump(), h);   // how it comes: how it is picked
    if (k) h = fnv("|" + JPXmlJson::from(k->toXml()).dump(), h);
    // Its footprint's land pattern and zero rotation (not its names).
    if (f) h = fnv("|" + JPXmlJson::from(f->geometry.toXml()).dump() + "|" + std::to_string(f->zeroRotationDeg), h);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

} // inline namespace jf
