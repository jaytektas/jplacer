// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSlotAutoFeeder.h"

inline namespace jf {

JPSlotAutoFeeder JPSlotAutoFeeder::fromXml(const JPXmlNode& own) {
    JPSlotAutoFeeder s;
    static_cast<JPAutoFeeder&>(s) = JPAutoFeeder::fromXml(own);
    if (const std::string* v = own.get("bank-id")) s.bankId = *v;
    if (const std::string* v = own.get("feeder-id")) s.feederId = *v;
    return s;
}

JPSlotAutoFeeder JPSlotAutoFeeder::fromJson(const JJson& j) {
    JPSlotAutoFeeder s;
    static_cast<JPAutoFeeder&>(s) = JPAutoFeeder::fromJson(j);
    s.bankId = j["bank"].str();
    s.feederId = j["feeder"].str();
    return s;
}

JJson JPSlotAutoFeeder::toJson() const {
    JJson j = JPAutoFeeder::toJson();
    j["bank"] = bankId;
    j["feeder"] = feederId;
    return j;
}

} // inline namespace jf
