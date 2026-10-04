// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardImporter.h"

inline namespace jf {

// Labcenter Proteus's pick and place file (.pkp), as OpenPnP's
// LabcenterProteusImporter reads it: lines opening with a quote are
// "Part ID","Value","Package",[Stock Code,]Layer,Rotation,X,Y; in mm,
// or thou when a line says "Units ... thou". A part is "Package-Value",
// assigned (and made when missing) only when asked.
class JPLabcenterProteusImporter : public JPBoardImporter {
public:
    std::string name() const override { return "Labcenter Proteus .pkp"; }
    std::string description() const override { return "Import Labcenter Proteus (.pkp) Pick amd Place Files."; }
    std::vector<File>   files() const override { return { { "Import File (.pkp)", { "pkp" } } }; }
    std::vector<Option> options() const override {
        return { { "Create Missing Parts", "", true }, { "Stock Codes Included", "", false } };
    }
    std::string failureText(const std::string&) const override {
        return "The expected file format is the default file export in Labcenter Proteus Data after header information "
               "should be :\nPart ID, Value, Package,[Stock Code,] Layer, Rotation, X, Y\nLikely cause: the number of "
               "data fields does not match expected input\nie: Include stock codes check box is not checked but file "
               "has stock codes";
    }

protected:
    void parse(const std::vector<std::string>& files, const std::vector<bool>& options, JPConfiguration& config,
               JPBoard& out) const override;
};

} // inline namespace jf
