// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerLauncher.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

inline namespace jf {

namespace {

namespace fs = std::filesystem;

constexpr const char* kEntryName = "jplacer.desktop";
constexpr const char* kIconName  = "jplacer.svg";
constexpr const char* kIconPng   = "jplacer.png";   // 256 px, beside the SVG at the AppDir root

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

// Write only when the content differs. True if it wrote.
bool writeIfChanged(const fs::path& p, const std::string& content) {
    std::error_code ec;
    if (fs::exists(p, ec) && readAll(p) == content) return false;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
    if (!f) {
        JLOGC(JPlacerLog::kDesktop, JLogLevel::Warn) << "could not write " << p.string();
        return false;
    }
    return true;
}

// The Desktop Entry spec's quoting for an Exec argument: double quotes, with
// " ` $ \ backslash-escaped and % doubled. "~/Downloads/my apps/jplacer.AppImage"
// is common enough that an unquoted Exec would simply not launch.
std::string execQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') out += '\\';
        if (c == '%') out += '%';
        out += c;
    }
    return out + "\"";
}

// The packed entry with Exec pointing at the AppImage itself -- not at the
// executable inside it, whose mount point changes on every launch.
std::string entryFor(const std::string& packed, const std::string& appimage) {
    std::istringstream in(packed);
    std::string out, line;
    while (std::getline(in, line)) {
        if (line.rfind("Exec=", 0) == 0) line = "Exec=" + execQuote(appimage);
        out += line + "\n";
    }
    return out;
}

// $XDG_DATA_HOME, else ~/.local/share.
fs::path dataHome() {
    if (const std::string x = env("XDG_DATA_HOME"); !x.empty()) return x;
    if (const std::string h = env("HOME"); !h.empty()) return fs::path(h) / ".local" / "share";
    return {};
}

fs::path entryPath(const fs::path& home)  { return home / "applications" / kEntryName; }
fs::path svgPath(const fs::path& home)    { return home / "icons" / "hicolor" / "scalable" / "apps" / kIconName; }
fs::path pngPath(const fs::path& home)    { return home / "icons" / "hicolor" / "256x256" / "apps" / kIconPng; }

// Most shells watch these folders; the explicit refresh covers the ones that
// only read their caches. Its absence or failure changes nothing that matters.
void refreshMenu(const fs::path& apps) {
    const int rc = std::system(("update-desktop-database -q \"" + apps.string() + "\" >/dev/null 2>&1").c_str());
    (void)rc;
}

// NO gtk-update-icon-cache. ~/.local/share/icons/hicolor has no index.theme of
// its own; a cache written there is trusted INSTEAD of the files, and every
// icon under it disappears (jscope found this out). Touching the directory is
// what the icon loaders check for changes.
void refreshIcons(const fs::path& home) {
    std::error_code ec;
    fs::last_write_time(home / "icons" / "hicolor", fs::file_time_type::clock::now(), ec);
}

} // namespace

bool JPlacerLauncher::supported() {
#if defined(__linux__)
    return !env("APPIMAGE").empty() && !env("APPDIR").empty();
#else
    return false;
#endif
}

bool JPlacerLauncher::install() {
    if (!supported()) return false;
    const fs::path home = dataHome();
    if (home.empty()) return false;
    const fs::path appdir = env("APPDIR");

    const std::string packed = readAll(appdir / kEntryName);
    if (packed.empty()) {
        JLOGC(JPlacerLog::kDesktop, JLogLevel::Warn) << "no " << kEntryName << " inside the AppImage";
        return false;
    }

    bool icons = false;
    if (const std::string svg = readAll(appdir / kIconName); !svg.empty()) icons |= writeIfChanged(svgPath(home), svg);
    if (const std::string png = readAll(appdir / kIconPng);  !png.empty()) icons |= writeIfChanged(pngPath(home), png);
    if (icons) refreshIcons(home);

    if (writeIfChanged(entryPath(home), entryFor(packed, env("APPIMAGE")))) {
        refreshMenu(home / "applications");
        JLOGC(JPlacerLog::kDesktop, JLogLevel::Info)
            << "applications menu entry written for " << env("APPIMAGE");
    }
    std::error_code ec;
    return fs::exists(entryPath(home), ec);
}

void JPlacerLauncher::remove() {
    const fs::path home = dataHome();
    if (home.empty()) return;
    std::error_code ec;
    const bool had = fs::remove(entryPath(home), ec);
    fs::remove(svgPath(home), ec);
    fs::remove(pngPath(home), ec);
    if (had) {
        refreshMenu(home / "applications");
        refreshIcons(home);
        JLOGC(JPlacerLog::kDesktop, JLogLevel::Info) << "applications menu entry removed";
    }
}

} // inline namespace jf
