// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>
//
// jplacer -- pick-and-place machine control.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// this program. If not, see <https://www.gnu.org/licenses/>.

#include "app/JPlacerApp.h"
#include "common/JPlacerLog.h"
#include "app/JPlacerSettings.h"

#include <j/core/Log.h>

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

using namespace jf;

namespace {

// --verbose / --trace <category> / --quiet turn the log thresholds up or down
// from the command line, without an edit-rebuild cycle: for this run, over
// what the console last chose. --settings <file> runs against another
// settings file, so a test never disturbs the real one. --help says so and
// starts nothing; an option it does not know (or one missing its argument)
// is refused the same way, rather than the app started as if it were not given.
struct JOptions {
    std::string               settingsPath = JPlacerSettings::defaultPath();
    std::optional<JLogLevel>  level;
    std::vector<std::string>  traced;
    bool                      help = false;
    std::string               error;   // what was wrong with the command line; empty: nothing
};

JOptions parseArgs(int argc, char** argv) {
    JOptions o;
    for (int i = 1; i < argc && o.error.empty(); ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            o.help = true;
        } else if (arg == "--verbose" || arg == "-v") {
            o.level = JLogLevel::Debug;
        } else if (arg == "--quiet" || arg == "-q") {
            o.level = JLogLevel::Warn;
        } else if (arg == "--trace" || arg == "--settings") {
            if (i + 1 >= argc) o.error = arg + " needs " + (arg == "--trace" ? "a category" : "a file");
            else if (arg == "--trace") o.traced.push_back(argv[++i]);
            else o.settingsPath = argv[++i];
        } else {
            o.error = "unknown option " + arg;
        }
    }
    return o;
}

// The command line's usage, the program's own output on the terminal (not a log message): the options, the
// log's categories (JPlacerLog's), and where the settings are kept.
std::string usage() {
    std::string u = "Usage: jplacer [options]\n"
                    "Pick-and-place machine control.\n\n"
                    "Options:\n"
                    "  -h, --help             Show this and exit.\n"
                    "  -v, --verbose          Log more detail.\n"
                    "  -q, --quiet            Log only warnings and errors.\n"
                    "      --trace <category> Log everything in one category (a wildcard takes an area: 'machine.*').\n"
                    "                         May be given more than once.\n"
                    "      --settings <file>  Use another settings file, leaving your own untouched.\n"
                    "                         Yours: " + JPlacerSettings::defaultPath() + "\n\n"
                    "Log categories:\n ";
    for (const std::string& c : JPlacerLog::all()) u += " " + c;
    return u + "\n";
}

} // namespace

int main(int argc, char** argv) {
    const JOptions opts = parseArgs(argc, argv);
    if (!opts.error.empty()) {
        std::fprintf(stderr, "jplacer: %s\n\n%s", opts.error.c_str(), usage().c_str());
        return 2;
    }
    if (opts.help) {
        std::fputs(usage().c_str(), stdout);
        return 0;
    }

    JPlacerApp app(opts.settingsPath);   // the log as last chosen
    if (opts.level) JLog::instance().setGlobalLevel(*opts.level);
    for (const std::string& c : opts.traced) JLog::instance().setLevel(c, JLogLevel::Trace);
    if (!app.valid()) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Error) << "application failed to initialise";
        return 1;
    }
    return app.run();
}
