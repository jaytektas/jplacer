// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerBlindsFiles.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>
#include <j/platform/JDesktop.h>

#include <filesystem>

inline namespace jf {

namespace fs = std::filesystem;

namespace {

const char* const kFiles[] = { "BlindsFeeder-Library.scad", "BlindsFeeder-3DPrinting.scad" };

// Where they are shipped: beside the executable, or (built) the source tree's.
fs::path source() {
    const fs::path exe = JPlacerPaths::exeDir();
    for (const fs::path& d : { exe / "openscad", exe / ".." / "openscad" }) {
        std::error_code ec;
        if (fs::is_directory(d, ec)) return d;
    }
    return {};
}

} // namespace

void JPlacerBlindsFiles::extract() {
    JDialog::openFolder("Extract 3D-Printing Files", [](std::string folder) {
        if (folder.empty()) return;
        const fs::path from = source();
        if (from.empty()) {
            JDialog::message("Error", "The OpenSCAD files are not installed beside jplacer.");
            return;
        }
        for (const char* name : kFiles)
            if (fs::exists(fs::path(folder) / name)) {
                JDialog::message("Error", "File " + (fs::path(folder) / name).string() + " already exists.");
                return;
            }
        bool opened = true;
        for (const char* name : kFiles) {
            std::error_code ec;
            const fs::path to = fs::path(folder) / name;
            if (!fs::copy_file(from / name, to, ec)) {
                JDialog::message("Error", "Cannot write " + to.string() + ": " + ec.message());
                return;
            }
            if (!JDesktop::openUrl(to.string())) {
                JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << "cannot open " << to.string();
                opened = false;
            }
        }
        if (!opened)
            JDialog::message("Files extracted", "Files extracted to:\n" + folder + "\nCannot open with OpenSCAD automatically (Desktop command failed)");
    });
}

} // inline namespace jf
