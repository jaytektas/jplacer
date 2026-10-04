// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardImporter.h"

inline namespace jf {

// OpenPnP's named-column CSV importer, which its Altium and Reference CSV
// importers are: the header line is looked for in the file's first 50
// lines, by the names each column may have (in capitals; a leading # is
// passed over), split on commas or else tabs. Reference, value, package, X,
// Y and rotation must be there; side, height and comment may be. A name
// ending "(MIL)" gives that column in mils. A part is "Package-Value";
// a placement whose part is not there (and is not made) is left out; one
// named FIDn or REFn is a fiducial.
class JPCsvImporter : public JPBoardImporter {
public:
    std::vector<File>   files() const override;
    std::vector<Option> options() const override;

    enum OptionIndex { CreateMissingParts, UpdateExistingPartHeights };

    struct Patterns {
        std::vector<std::string> reference, value, package, x, y, rotation, side, height, comment;
    };
    virtual Patterns patterns() const = 0;

protected:
    void parse(const std::vector<std::string>& files, const std::vector<bool>& options, JPConfiguration& config,
               JPBoard& out) const override;
};

} // inline namespace jf
