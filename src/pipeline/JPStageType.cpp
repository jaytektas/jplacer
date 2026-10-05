// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStageType.h"

inline namespace jf {

std::string JPStageType::typeName() const {
    const size_t dot = className.rfind('.');
    return dot == std::string::npos ? className : className.substr(dot + 1);
}

const JPStageType::Property* JPStageType::property(const std::string& attribute) const {
    for (const Property& p : properties)
        if (p.attribute == attribute) return &p;
    return nullptr;
}

} // inline namespace jf
