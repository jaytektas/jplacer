// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLeverFeeder.h"

inline namespace jf {

JPLeverFeeder JPLeverFeeder::fromXml(const JPXmlNode& own) {
    JPLeverFeeder l;
    l.readXml(own);
    return l;
}

JPLeverFeeder JPLeverFeeder::fromJson(const JJson& j) {
    JPLeverFeeder l;
    l.readJson(j);
    return l;
}

} // inline namespace jf
