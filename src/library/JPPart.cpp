// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPart.h"

#include <algorithm>

inline namespace jf {

bool JPPart::hasNumber(const JPSupplierNumber& n) const {
    return std::find(supplierNumbers.begin(), supplierNumbers.end(), n) != supplierNumbers.end();
}

JPPart JPPart::fromJson(const JJson& j) {
    JPPart p;
    p.id           = j["id"].str();
    p.mpn          = j["mpn"].str();
    p.manufacturer = j["manufacturer"].str();
    for (const JJson& n : j["supplierNumbers"].arr())
        p.supplierNumbers.push_back({ n["supplier"].str(), n["number"].str() });
    p.value        = j["value"].str();
    p.tolerance    = j["tolerance"].str();
    p.voltage      = j["voltage"].str();
    p.power        = j["power"].str();
    p.dielectric   = j["dielectric"].str();
    p.temperature  = j["temperature"].str();
    p.description  = j["description"].str();
    p.packageId    = j["packageId"].str();
    p.revision     = int(j["revision"].number(1.0));
    if (j.contains("origin")) p.origin = JPOrigin::fromJson(j["origin"]);
    return p;
}

JJson JPPart::toJson() const {
    JJson j = JJson::object();
    j["id"]           = id;
    j["mpn"]          = mpn;
    j["manufacturer"] = manufacturer;
    JJson ns = JJson::array();
    for (const JPSupplierNumber& n : supplierNumbers) {
        JJson o = JJson::object();
        o["supplier"] = n.supplier;
        o["number"]   = n.number;
        ns.push(std::move(o));
    }
    j["supplierNumbers"] = std::move(ns);
    j["value"]        = value;
    j["tolerance"]    = tolerance;
    j["voltage"]      = voltage;
    j["power"]        = power;
    j["dielectric"]   = dielectric;
    j["temperature"]  = temperature;
    j["description"]  = description;
    j["packageId"]    = packageId;
    j["revision"]     = revision;
    if (origin.fromLibrary()) j["origin"] = origin.toJson();
    return j;
}

} // inline namespace jf
