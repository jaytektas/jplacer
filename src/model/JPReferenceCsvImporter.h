// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCsvImporter.h"

inline namespace jf {

// OpenPnP's Reference CSV importer: named columns, by the many names CAD
// tools give them.
class JPReferenceCsvImporter : public JPCsvImporter {
public:
    std::string name() const override { return "Reference CSV"; }
    std::string description() const override { return "Import Named Comma Separated Values Files."; }
    Patterns patterns() const override;
};

} // inline namespace jf
