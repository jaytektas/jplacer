// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPAutoFeeder.h"

#include <string>

inline namespace jf {

// A slot auto feeder (OpenPnP's ReferenceSlotAutoFeeder): a place on the machine an auto feeder of a bank
// (JPSlotBanks) is loaded into, fed as an auto feeder is. What an auto feeder has, and the bank its feeders come
// from and the bank's feeder loaded in it (none: empty).
class JPSlotAutoFeeder : public JPAutoFeeder {
public:
    static constexpr const char* kClass = "org.openpnp.machine.reference.feeder.ReferenceSlotAutoFeeder";
    static constexpr const char* kKey = "slotAuto";

    std::string bankId, feederId;

    // OpenPnP's settings of it (its <feeder>'s own, what every feeder has taken out).
    static JPSlotAutoFeeder fromXml(const JPXmlNode& own);
    // As jplacer's feeders.json keeps it: an auto feeder's, and {"bank", "feeder"}.
    static JPSlotAutoFeeder fromJson(const JJson& j);
    JJson toJson() const;
};

} // inline namespace jf
