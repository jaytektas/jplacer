// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerHelpPages.h"

#include "JPlacerLog.h"

#include <j/core/Log.h>
#include <j/io/JLocalWebServer.h>
#include <j/platform/JDesktop.h>

#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

inline namespace jf {

namespace {

namespace fs = std::filesystem;

fs::path exeDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? fs::path(std::wstring(buf, n)).parent_path() : fs::path();
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : exe.parent_path();
#endif
}

} // namespace

std::string JPlacerHelpPages::manualDir() {
    const fs::path exe = exeDir();
    for (const fs::path& d : { exe / "manual",                        // shipped beside the executable
                               exe / ".." / "manual" / "site" }) {   // build/jplacer -> manual/site
        std::error_code ec;
        if (fs::exists(d / "index.html", ec)) return fs::weakly_canonical(d, ec).string();
    }
    return {};
}

bool JPlacerHelpPages::openPage(const std::string& page, std::string& error) {
    static JLocalWebServer web;
    static bool mounted = false;
    if (!mounted) {
        const std::string dir = manualDir();
        if (dir.empty()) {
            error = "The user manual is not installed with this copy of jplacer";
            return false;
        }
        web.mount("manual", dir);
        mounted = true;
    }
    if (!web.start(error)) return false;
    const std::string url = web.url("manual/" + page);
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "opening " << url;
    if (!JDesktop::openUrl(url)) {
        error = "No browser could be opened \xE2\x80\x94 the manual is at " + url;
        return false;
    }
    return true;
}

bool JPlacerHelpPages::openManual(std::string& error)   { return openPage("index.html", error); }
bool JPlacerHelpPages::openWhatsNew(std::string& error) { return openPage("whats-new.html", error); }

} // inline namespace jf
