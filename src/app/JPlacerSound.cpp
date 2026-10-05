// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerSound.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"

#include <j/core/Log.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

inline namespace jf {

namespace {

constexpr int    kRate = 22050;
constexpr double kPi = 3.14159265358979323846;
// Each note rises and falls this fast (in seconds), so it does not click.
constexpr double kEdge = 0.01;
constexpr double kLevel = 0.35;   // of full scale

const char* fileName(JPlacerSound::Sound s) {
    return s == JPlacerSound::Sound::Error ? "error.wav" : "success.wav";
}

void put16(std::string& out, uint16_t v) {
    out += char(v & 0xff);
    out += char(v >> 8);
}

void put32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out += char((v >> (8 * i)) & 0xff);
}

// The first of the desktop's players there is on the PATH.
std::string player() {
    const char* path = std::getenv("PATH");
    if (!path) return {};
    for (const char* name : { "pw-play", "paplay", "aplay" }) {
        std::string dirs = path;
        size_t start = 0;
        while (start <= dirs.size()) {
            const size_t end = dirs.find(':', start);
            const std::string dir = dirs.substr(start, end == std::string::npos ? std::string::npos : end - start);
            const std::filesystem::path p = std::filesystem::path(dir.empty() ? "." : dir) / name;
            if (::access(p.c_str(), X_OK) == 0) return p.string();
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    return {};
}

} // namespace

std::string JPlacerSound::wav(Sound sound) {
    // Notes (Hz) and how long each lasts (s): a falling pair for an error, a rising chord for success.
    struct Note { double hz, seconds; };
    const std::vector<Note> notes = sound == Sound::Error
        ? std::vector<Note> { { 440.0, 0.18 }, { 311.1, 0.32 } }
        : std::vector<Note> { { 523.3, 0.12 }, { 659.3, 0.12 }, { 784.0, 0.28 } };
    std::vector<int16_t> samples;
    for (const Note& n : notes) {
        const int count = int(n.seconds * kRate);
        for (int i = 0; i < count; ++i) {
            const double t = double(i) / kRate;
            const double edge = std::min({ 1.0, t / kEdge, (n.seconds - t) / kEdge });
            // A little of the second harmonic, so it is heard on small speakers.
            const double v = std::sin(2 * kPi * n.hz * t) + 0.3 * std::sin(4 * kPi * n.hz * t);
            samples.push_back(int16_t(std::lround(kLevel / 1.3 * edge * v * 32767)));
        }
    }
    std::string out;
    const uint32_t dataBytes = uint32_t(samples.size() * 2);
    out += "RIFF";
    put32(out, 36 + dataBytes);
    out += "WAVEfmt ";
    put32(out, 16);
    put16(out, 1);                 // PCM
    put16(out, 1);                 // mono
    put32(out, kRate);
    put32(out, kRate * 2);         // bytes a second
    put16(out, 2);                 // bytes a frame
    put16(out, 16);                // bits a sample
    out += "data";
    put32(out, dataBytes);
    for (const int16_t s : samples) put16(out, uint16_t(s));
    return out;
}

std::string JPlacerSound::file(Sound sound) {
    const std::filesystem::path own = std::filesystem::path(JPlacerPaths::configDir()) / "sounds" / fileName(sound);
    std::error_code ec;
    if (std::filesystem::is_regular_file(own, ec)) return own.string();
    static std::mutex lock;
    const std::lock_guard<std::mutex> hold(lock);
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "jplacer-sounds";
    const std::filesystem::path p = dir / fileName(sound);
    if (std::filesystem::is_regular_file(p, ec)) return p.string();
    std::filesystem::create_directories(dir, ec);
    std::ofstream f(p, std::ios::binary);
    f << wav(sound);
    if (!f) return {};
    return p.string();
}

void JPlacerSound::play(Sound sound) {
    static const std::string program = player();
    if (program.empty()) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "no sound played: none of pw-play, paplay, aplay is installed";
        return;
    }
    const std::string path = file(sound);
    if (path.empty()) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "no sound played: its file could not be written";
        return;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    std::vector<char*> argv { const_cast<char*>(program.c_str()), const_cast<char*>(path.c_str()), nullptr };
    pid_t pid = 0;
    const int failed = posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (failed) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "no sound played: " << program << " could not be started";
        return;
    }
    // Reaped when it ends, not waited for.
    std::thread([pid] { ::waitpid(pid, nullptr, 0); }).detach();
}

} // inline namespace jf
