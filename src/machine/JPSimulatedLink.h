// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLink.h"
#include "JPGcodeServer.h"
#include "JPSimulatedGrbl.h"

inline namespace jf {

// A link to an in-process simulated controller: a Grbl-family one (JPSimulatedGrbl), or, with "kind":
// "gcodeServer" in its config, OpenPnP's generic G-code one (JPGcodeServer).
class JPSimulatedLink : public JPLink {
public:
    explicit JPSimulatedLink(const JJson& config);

    bool open(std::string& error) override;
    void close() override;
    bool isOpen() const override;
    bool write(const std::string& bytes) override;
    std::optional<std::string> readLine(int timeoutMs) override;
    std::string describe() const override;

private:
    bool hasOutput() const;
    std::string takeLine();

    bool            m_gcodeServer = false;
    JPSimulatedGrbl m_controller;
    JPGcodeServer   m_server;
    bool            m_open = false;
};

} // inline namespace jf
