// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLink.h"

#include <memory>
#include <string>

inline namespace jf {

class JJson;

// Builds the link a controller's configuration names ("link": { "type": ... }).
class JPLinkFactory {
public:
    // Nothing, with `error`, for an unknown type or missing settings.
    static std::unique_ptr<JPLink> create(const JJson& link, std::string& error);
};

} // inline namespace jf
