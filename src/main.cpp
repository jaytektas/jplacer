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

#include <string>

using namespace jf;

namespace {

// --verbose / --trace <category> / --quiet turn the log thresholds up or down
// from the command line, without an edit-rebuild cycle. --settings <file> runs
// against another settings file, so a test never disturbs the real one.
struct JOptions { std::string settingsPath = JPlacerSettings::defaultPath(); };

JOptions parseArgs(int argc, char** argv) {
    JOptions o;
    JLog::instance().setGlobalLevel(JLogLevel::Info);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            JLog::instance().setGlobalLevel(JLogLevel::Debug);
        } else if (arg == "--quiet" || arg == "-q") {
            JLog::instance().setGlobalLevel(JLogLevel::Warn);
        } else if (arg == "--trace" && i + 1 < argc) {
            JLog::instance().setLevel(argv[++i], JLogLevel::Trace);
        } else if (arg == "--settings" && i + 1 < argc) {
            o.settingsPath = argv[++i];
        }
    }
    return o;
}

} // namespace

int main(int argc, char** argv) {
    const JOptions opts = parseArgs(argc, argv);

    JPlacerApp app(opts.settingsPath);
    if (!app.valid()) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Error) << "application failed to initialise";
        return 1;
    }
    return app.run();
}
