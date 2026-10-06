// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLinkFactory.h"

#include "JPNeoden4Link.h"
#include "JPSerialLink.h"
#include "JPSimulatedLink.h"
#include "JPTcpLink.h"

#include <j/config/Json.h>

inline namespace jf {

std::unique_ptr<JPLink> JPLinkFactory::create(const JJson& link, std::string& error) {
    const std::string& type = link["type"].str();
    // A serial port's settings, as OpenPnP's serial settings are.
    auto serial = [&](JPSerialLink::Settings& st) {
        st.port = link["port"].str();
        st.baud = int(link["baud"].number());
        if (st.port.empty() || st.baud <= 0) {
            error = "a serial link needs a port and a baud rate";
            return false;
        }
        st.flow = link["flowControl"].str();
        // Left out: as nearly every controller is (8 data bits, 1 stop bit, no parity, lines left be).
        st.dataBits = int(link["dataBits"].number(8));
        st.stopBits = int(link["stopBits"].number(1));
        st.parity = link["parity"].str();
        st.setDtr = link["setDtr"].boolean();
        st.setRts = link["setRts"].boolean();
        return true;
    };
    if (type == "serial") {
        JPSerialLink::Settings st;
        if (!serial(st)) return nullptr;
        return std::make_unique<JPSerialLink>(std::move(st));
    }
    if (type == "neoden4") {
        // OpenPnP's NeoDen4Driver: its serial port, and the scale of X and Y.
        JPNeoden4Link::Settings st;
        if (!serial(st.serial)) return nullptr;
        st.scaleX = link["scaleX"].number(JPNeoden4Link::kScaleX);
        st.scaleY = link["scaleY"].number(JPNeoden4Link::kScaleY);
        return std::make_unique<JPNeoden4Link>(std::move(st));
    }
    if (type == "simulated") return std::make_unique<JPSimulatedLink>(link["simulator"]);
    if (type == "tcp") {
        const std::string& host = link["host"].str();
        const int port = int(link["port"].number(JPTcpLink::kDefaultPort));
        if (host.empty() || port <= 0) {
            error = "a TCP link needs an IP address (or host name) and a port";
            return nullptr;
        }
        return std::make_unique<JPTcpLink>(host, port);
    }
    error = "unknown link type '" + type + "'";
    return nullptr;
}

} // inline namespace jf
