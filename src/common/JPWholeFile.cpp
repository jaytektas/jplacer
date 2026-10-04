// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPWholeFile.h"

#include <filesystem>
#include <fstream>
#include <system_error>

inline namespace jf {

namespace fs = std::filesystem;

bool JPWholeFile::write(const std::string& path, const std::string& text, std::string& error) {
    std::error_code ec;
    if (const fs::path dir = fs::path(path).parent_path(); !dir.empty()) fs::create_directories(dir, ec);
    const std::string tmp = path + ".new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        out.flush();
        if (!out) {
            error = tmp + ": could not be written";
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        error = path + ": could not be replaced (" + ec.message() + ")";
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

} // inline namespace jf
