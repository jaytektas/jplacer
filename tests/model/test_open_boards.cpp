// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The boards and panels open are the open job's (and those opened beside it): none at start, whatever
// OpenPnP's boards.xml in the folder lists; a job brings its own; closing them all takes every one away, and
// what was open can be had open again (a job that could not be read).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"

#include <filesystem>
#include <fstream>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-open-boards";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string boardFile = (dir / "a.jpboard").string();

    // A board made, so a file is there; and an old boards.xml naming it.
    {
        JPConfiguration config(dir.string());
        std::string error;
        assert(config.board(boardFile, error) && config.boards().size() == 1);
        std::ofstream(dir / "boards.xml") << "<openpnp-boards><board>" << boardFile << "</board></openpnp-boards>\n";
    }
    // At start, nothing open: boards.xml is not what opens boards.
    JPConfiguration config(dir.string());
    std::vector<std::string> problems;
    std::string error;
    assert(config.load(problems, error) && config.boards().empty() && config.panels().empty());

    // Opened beside the job, then all closed: none open, the one closed still alive for those holding it.
    auto b = config.board(boardFile, error);
    assert(b && config.boards().size() == 1);
    JPBoard* raw = b.get();
    b.reset();
    JPConfiguration::Open closed = config.closeAll();
    assert(config.boards().empty() && closed.boards.size() == 1 && closed.boards[0].get() == raw);
    // Had open again: the same board.
    config.reopen(std::move(closed));
    assert(config.boards().size() == 1 && config.boards()[0].get() == raw);

    // Saving the configuration writes no boards' list.
    fs::remove(dir / "boards.xml");
    assert(config.save(error) && !fs::exists(dir / "boards.xml") && !fs::exists(dir / "panels.xml"));
    fs::remove_all(dir);
    return 0;
}
