// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// JPBackups: what a run starts from copied, a rolling set. Nothing there: none taken. The settings file and
// every cell copied into backups/<date-time>/ (cells under cells/), the originals untouched; past `keep`, the
// oldest let go.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "common/JPBackups.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace jf;

int main() {
    const fs::path dir = fs::temp_directory_path() / ("jplacer-backups-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string where, why;
    const std::string settings = (dir / "jplacer.json").string();
    assert(!JPBackups::take(dir.string(), settings, 3, where, why) && !why.empty());   // nothing yet
    std::ofstream(settings) << "{\"issues.milestone\": \"Vision\"}";
    fs::create_directories(dir / "cells");
    std::ofstream(dir / "cells" / "openpnp.json") << "{\"name\": \"bench\"}";
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));   // a name of its own each
        assert(JPBackups::take(dir.string(), settings, 3, where, why));
        assert(fs::exists(fs::path(where) / "jplacer.json") && fs::exists(fs::path(where) / "cells" / "openpnp.json"));
    }
    int kept = 0;
    for (const auto& e : fs::directory_iterator(dir / "backups")) kept += e.is_directory() ? 1 : 0;
    assert(kept == 3);
    assert(fs::exists(where));   // the newest is among them
    assert(fs::file_size(settings) > 0 && fs::exists(dir / "cells" / "openpnp.json"));   // the originals as they were
    fs::remove_all(dir);
    return 0;
}
