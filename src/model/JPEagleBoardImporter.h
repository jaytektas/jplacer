// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardImporter.h"

inline namespace jf {

// An EAGLE board file (.brd), read directly, as OpenPnP's
// EagleBoardImporter reads it: each element a placement (mirrored, "M…",
// on the bottom); its package's SMD pads the footprint of a package made
// or updated (as asked), named "Package" or "Library-Package"; a part
// "Package-Value"; and each pad that takes cream a solder paste pad on the
// board.
class JPEagleBoardImporter : public JPBoardImporter {
public:
    std::string name() const override { return "CadSoft EAGLE Board"; }
    std::string description() const override { return "Import files directly from EAGLE's <filename>.brd file."; }
    std::vector<File> files() const override { return { { "Eagle PCB Board File (.brd)", { "brd" } } }; }
    std::vector<Option> options() const override;

    enum OptionIndex { CreateMissingParts, UpdateExistingParts, AddLibraryPrefix, ImportTop, ImportBottom };

protected:
    void parse(const std::vector<std::string>& files, const std::vector<bool>& options, JPConfiguration& config,
               JPBoard& out) const override;
};

} // inline namespace jf
