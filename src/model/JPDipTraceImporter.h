// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardImporter.h"

inline namespace jf {

// DipTrace's pick and place export, as OpenPnP's DipTraceImporter reads it:
// a header line, then "RefDes,Name,X (mm),Y (mm),Side,Rotate,Value"; a part
// is "Name-Value", assigned (and made when missing) only when asked.
class JPDipTraceImporter : public JPBoardImporter {
public:
    std::string name() const override { return "Diptrace .csv"; }
    std::string description() const override { return "Import Diptrace .csv Files."; }
    std::vector<File>   files() const override { return { { "Export File (.csv)", { "csv" } } }; }
    std::vector<Option> options() const override { return { { "Create Missing Parts", "", true } }; }
    std::string failureText(const std::string&) const override {
        return "The expected file format is the default file export in DipTrace PCB: File -> Export -> Pick and Place. "
               "The first line indicates RefDes, Name, X (mm), Y (mm), Side, Rotate, Value.The lines that follow are "
               "data.";
    }

protected:
    void parse(const std::vector<std::string>& files, const std::vector<bool>& options, JPConfiguration& config,
               JPBoard& out) const override;
};

} // inline namespace jf
