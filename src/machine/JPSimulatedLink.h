// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLink.h"
#include "JPSimulatedGrbl.h"

inline namespace jf {

// A link to an in-process simulated controller (JPSimulatedGrbl).
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
    JPSimulatedGrbl m_controller;
    bool            m_open = false;
};

} // inline namespace jf
