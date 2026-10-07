// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionDebug.h"

#include "JPPipeline.h"
#include "JPStageUtil.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

inline namespace jf {

namespace {

std::mutex  g_mutex;
std::string g_directory;

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
    g_directory = directory;
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
    return d.empty() ? std::string() : (std::filesystem::path(d) / "org.openpnp.vision.pipeline.stages.ImageWriteDebug").string();
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
}

} // inline namespace jf
