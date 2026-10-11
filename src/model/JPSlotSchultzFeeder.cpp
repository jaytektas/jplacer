// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSlotSchultzFeeder.h"

inline namespace jf {

JPSlotSchultzFeeder JPSlotSchultzFeeder::fromXml(const JPXmlNode& own) {
    JPSlotSchultzFeeder s;
    static_cast<JPSchultzFeeder&>(s) = JPSchultzFeeder::fromXml(own);
    if (const std::string* v = own.get("bank-id")) s.bankId = *v;
    if (const std::string* v = own.get("feeder-id")) s.feederId = *v;
    return s;
}

JPSlotSchultzFeeder JPSlotSchultzFeeder::fromJson(const JJson& j) {
    JPSlotSchultzFeeder s;
    static_cast<JPSchultzFeeder&>(s) = JPSchultzFeeder::fromJson(j);
    s.bankId = j["bank"].str();
    s.feederId = j["feeder"].str();
    return s;
}

JJson JPSlotSchultzFeeder::toJson() const {
    JJson j = JPSchultzFeeder::toJson();
    j["bank"] = bankId;
    j["feeder"] = feederId;
    return j;
}

} // inline namespace jf
