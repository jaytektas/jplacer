// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardImporter.h"

inline namespace jf {

// KiCad's .pos files (top and bottom), as OpenPnP's KicadPosImporter reads
// them: a placement a line, "Ref Val Package PosX PosY Rot Side" in mm; a
// bottom one's X turned over (KiCad gives it from the other edge) and its
// rotation made 180 less it. A part is "Package-Value" (or the value
// alone), assigned when there is one, made when asked.
class JPKicadPosImporter : public JPBoardImporter {
public:
    std::string name() const override { return "KiCAD .pos"; }
    std::string description() const override { return "Import KiCAD .pos Files."; }
    std::vector<File>   files() const override;
    std::vector<Option> options() const override;

    enum OptionIndex { AssignParts, CreateMissingParts, UseOnlyValueAsPartId };

protected:
    void parse(const std::vector<std::string>& files, const std::vector<bool>& options, JPConfiguration& config,
               JPBoard& out) const override;

private:
    static void parseFile(const std::string& path, JPSide side, const std::vector<bool>& options,
                          JPConfiguration& config, JPBoard& out);
};

} // inline namespace jf
