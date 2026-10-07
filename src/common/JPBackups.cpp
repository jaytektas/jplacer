// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBackups.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>

inline namespace jf {

namespace fs = std::filesystem;

bool JPBackups::take(const std::string& configDir, const std::string& settingsFile, int keep, std::string& where,
                     std::string& why) {
    std::error_code ec;
    const fs::path backups = fs::path(configDir) / "backups";
    // Named by when, to the millisecond: one a start, in order by name.
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    char stamp[48];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&t));
    const fs::path dir = backups / (std::string(stamp) + "." + std::to_string(1000 + ms).substr(1));
    std::vector<fs::path> files;
    if (fs::is_regular_file(settingsFile, ec)) files.push_back(settingsFile);
    for (const auto& e : fs::directory_iterator(fs::path(configDir) / "cells", ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".json") files.push_back(e.path());
    if (files.empty()) {
        why = "nothing to back up yet";
        return false;
    }
    fs::create_directories(dir / "cells", ec);
    if (ec) {
        why = "cannot make " + dir.string() + ": " + ec.message();
        return false;
    }
    for (const fs::path& f : files) {
        const fs::path to = f.parent_path().filename() == "cells" ? dir / "cells" / f.filename() : dir / f.filename();
        fs::copy_file(f, to, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            why = "cannot copy " + f.string() + ": " + ec.message();
            return false;
        }
    }
    where = dir.string();
    // The newest `keep` kept.
    std::vector<fs::path> taken;
    for (const auto& e : fs::directory_iterator(backups, ec))
        if (e.is_directory(ec)) taken.push_back(e.path());
    std::sort(taken.begin(), taken.end());
    for (size_t i = 0; keep > 0 && taken.size() > size_t(keep) && i < taken.size() - size_t(keep); ++i) fs::remove_all(taken[i], ec);
    return true;
}

} // inline namespace jf
