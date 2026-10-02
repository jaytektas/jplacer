// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPFrame.h"

#include <string>

inline namespace jf {

// Pictures to and from files. PNG: lossless, so a saved picture measures the
// same as the live one did, and every viewer opens it.
class JPImageFile {
public:
    // False with `error` (the file and the reason) on failure.
    static bool writePng(const std::string& path, const JPFrame& frame, std::string& error);
    static bool readPng(const std::string& path, JPFrame& frame, std::string& error);
};

} // inline namespace jf
