// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionDebug.h"

#include "JPPipeline.h"
#include "JPStageUtil.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

inline namespace jf {

namespace {

std::mutex  g_mutex;
std::string g_directory;
std::uintmax_t g_limit = 0;

// What has been written, oldest first, and all it comes to: counted from the disk once (g_counted), then kept.
struct Kept {
    std::filesystem::path           path;
    std::uintmax_t                  bytes = 0;
    std::filesystem::file_time_type when;
};
std::vector<Kept> g_kept;
std::uintmax_t    g_held = 0;
bool              g_counted = false;

std::uintmax_t sizeOf(const std::filesystem::path& p) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) return std::filesystem::file_size(p, ec);
    std::uintmax_t n = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(p, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec))
        if (it->is_regular_file(ec)) n += it->file_size(ec);
    return n;
}

std::filesystem::path runsDir(const std::string& d) { return std::filesystem::path(d) / "log" / "vision"; }
std::filesystem::path writesDir(const std::string& d) {
    return std::filesystem::path(d) / "org.openpnp.vision.pipeline.stages.ImageWriteDebug";
}

// Under the lock: what is on the disk counted (each run's folder, each ImageWriteDebug picture), oldest first.
void countLocked() {
    if (g_counted || g_directory.empty()) return;
    g_kept.clear();
    g_held = 0;
    for (const auto& dir : { runsDir(g_directory), writesDir(g_directory) }) {
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator();
             it.increment(ec)) {
            Kept k { it->path(), sizeOf(it->path()), it->last_write_time(ec) };
            g_held += k.bytes;
            g_kept.push_back(std::move(k));
        }
    }
    std::sort(g_kept.begin(), g_kept.end(), [](const Kept& a, const Kept& b) { return a.when < b.when; });
    g_counted = true;
}

// Under the lock: the oldest let go while it holds more than the limit (the newest always kept).
void trimLocked() {
    if (g_limit == 0) return;
    size_t gone = 0;
    while (g_held > g_limit && gone + 1 < g_kept.size()) {
        std::error_code ec;
        std::filesystem::remove_all(g_kept[gone].path, ec);
        g_held -= std::min(g_held, g_kept[gone].bytes);
        ++gone;
    }
    g_kept.erase(g_kept.begin(), g_kept.begin() + std::ptrdiff_t(gone));
}

void keepLocked(const std::filesystem::path& p) {
    countLocked();
    std::error_code ec;
    Kept k { p, sizeOf(p), std::filesystem::last_write_time(p, ec) };
    g_held += k.bytes;
    g_kept.push_back(std::move(k));
    trimLocked();
}

// OpenPnP's debug picture time ("YYYY-MM-dd'T'HH.mm.ss.SSS"), safe in a file name.
std::string stamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm {};
    localtime_r(&t, &tm);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d.%02d.%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    return buf;
}

// A name as a file name: letters, digits, '-', '_' and '.'; the rest '_'.
std::string fileSafe(const std::string& s) {
    std::string out = s.empty() ? std::string("pipeline") : s;
    for (char& c : out)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) c = '_';
    return out;
}

} // namespace

void JPVisionDebug::setDirectory(const std::string& directory) {
    std::lock_guard lk(g_mutex);
    if (directory != g_directory) g_counted = false;   // counted again from the disk, there
    g_directory = directory;
}

void JPVisionDebug::setLimit(std::uintmax_t bytes) {
    std::lock_guard lk(g_mutex);
    g_limit = bytes;
    if (g_directory.empty()) return;
    countLocked();
    trimLocked();
}

std::uintmax_t JPVisionDebug::limit() {
    std::lock_guard lk(g_mutex);
    return g_limit;
}

std::uintmax_t JPVisionDebug::held() {
    std::lock_guard lk(g_mutex);
    countLocked();
    return g_held;
}

void JPVisionDebug::wrote(const std::string& path) {
    std::lock_guard lk(g_mutex);
    if (g_directory.empty()) return;
    keepLocked(path);
}

bool JPVisionDebug::on() {
    std::lock_guard lk(g_mutex);
    return !g_directory.empty();
}

std::string JPVisionDebug::directory() {
    std::lock_guard lk(g_mutex);
    return g_directory;
}

std::string JPVisionDebug::imageWriteDebugDirectory() {
    const std::string d = directory();
    return d.empty() ? std::string() : writesDir(d).string();
}

void JPVisionDebug::saveRun(const JPPipeline& pipeline, const std::string& what) {
    const std::string d = directory();
    if (d.empty()) return;
    const std::filesystem::path folder = std::filesystem::path(d) / "log" / "vision" / (stamp() + "_" + fileSafe(what));
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) return;
    std::ofstream list(folder / "stages.txt");
    int i = 0;
    for (const JPPipelineStage& stage : pipeline.stages()) {
        ++i;
        const JPPipeline::Result* r = pipeline.result(stage.name());
        char n[16];
        std::snprintf(n, sizeof n, "%02d", i);
        if (r && !r->image.empty())
            JPStageUtil::writePicture((folder / (std::string(n) + "_" + fileSafe(stage.name()) + ".png")).string(), r->image);
        list << n << " " << stage.name() << " (" << stage.className() << ")" << (stage.enabled() ? "" : ", not enabled");
        if (r) {
            char ms[32];
            std::snprintf(ms, sizeof ms, ", %.1f ms", r->milliseconds);
            list << ms;
            const std::string said = r->model.describe();
            if (!said.empty()) list << ": " << said;
        }
        list << "\n";
    }
    list.close();
    std::lock_guard lk(g_mutex);
    if (g_directory == d) keepLocked(folder);
}

} // inline namespace jf
