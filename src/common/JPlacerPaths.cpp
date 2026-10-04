// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPaths.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

inline namespace jf {

std::string JPlacerPaths::exeDir() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return n ? fs::path(std::wstring(buf, n)).parent_path().string() : std::string();
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : exe.parent_path().string();
#endif
}

std::string JPlacerPaths::configDir() {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    if (const char* appData = std::getenv("APPDATA"))
        return (fs::path(appData) / "jplacer").string();
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return (fs::path(xdg) / "jplacer").string();
    if (const char* home = std::getenv("HOME"))
        return (fs::path(home) / ".config" / "jplacer").string();
#endif
    return {};
}

} // inline namespace jf
