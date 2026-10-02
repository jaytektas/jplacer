// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLinkFactory.h"

#include "JPSerialLink.h"
#include "JPSimulatedLink.h"

#include <j/config/Json.h>

inline namespace jf {

std::unique_ptr<JPLink> JPLinkFactory::create(const JJson& link, std::string& error) {
    const std::string& type = link["type"].str();
    if (type == "serial") {
        const std::string& port = link["port"].str();
        const int baud = int(link["baud"].number());
        if (port.empty() || baud <= 0) {
            error = "a serial link needs a port and a baud rate";
            return nullptr;
        }
        return std::make_unique<JPSerialLink>(port, baud);
    }
    if (type == "simulated") return std::make_unique<JPSimulatedLink>(link["simulator"]);
    error = "unknown link type '" + type + "'";
    return nullptr;
}

} // inline namespace jf
