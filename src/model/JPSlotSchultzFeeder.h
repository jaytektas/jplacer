// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSchultzFeeder.h"

#include <string>

inline namespace jf {

// A slot Schultz feeder (OpenPnP's SlotSchultzFeeder): a place on the machine a Schultz feeder of a bank
// (JPSlotBanks) is loaded into, fed as a Schultz feeder is. What a Schultz feeder has, and the bank its feeders come
// from and the bank's feeder loaded in it (none: empty).
class JPSlotSchultzFeeder : public JPSchultzFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.SlotSchultzFeeder";
    static constexpr const char* kKey = "slotSchultz";

    std::string bankId, feederId;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPSlotSchultzFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: a Schultz feeder's, and {"bank", "feeder"}.
    static JPSlotSchultzFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
