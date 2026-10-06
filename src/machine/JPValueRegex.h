// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <optional>
#include <regex>
#include <string>

inline namespace jf {

// A regex as OpenPnP writes one to read a value from a reply (Java's): named groups allowed, the value in the
// group named "Value" (OpenPnP's ACTUATOR_READ_REGEX and HttpActuator's regex). std::regex has no names, so they
// become plain groups and the Value group is found by counting.
class JPValueRegex {
public:
    // False when `pattern` is not a valid regex.
    bool compile(const std::string& pattern);
    const std::regex& regex() const { return m_regex; }
    // The Value group's number; none when the pattern names none.
    std::optional<size_t> valueGroup() const { return m_valueGroup; }

private:
    std::regex            m_regex;
    std::optional<size_t> m_valueGroup;
};

} // inline namespace jf
