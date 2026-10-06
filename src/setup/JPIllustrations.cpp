// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPIllustrations.h"

#include "camera/JPImageFile.h"
#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"

#include <j/core/Log.h>

#include <filesystem>
#include <map>
#include <mutex>

inline namespace jf {

std::shared_ptr<const JPFrame> JPIllustrations::picture(const std::string& fileName) {
    namespace fs = std::filesystem;
    static std::mutex m;
    static std::map<std::string, std::shared_ptr<const JPFrame>> read;
    std::lock_guard lk(m);
    if (const auto it = read.find(fileName); it != read.end()) return it->second;
    const fs::path exe = JPlacerPaths::exeDir();
    std::shared_ptr<JPFrame> frame;
    for (const fs::path& d : { exe / "illustrations", exe / ".." / "illustrations" }) {   // shipped beside it; build/ -> illustrations/
        if (!fs::exists(d / fileName)) continue;
        auto f = std::make_shared<JPFrame>();
        std::string error;
        if (JPImageFile::readPng((d / fileName).string(), *f, error)) {
            // Drawn for a light page (black lettering on nothing): laid on white paper, so any theme shows it.
            constexpr int kPaper = 255;
            for (size_t i = 0; i + 3 < f->rgba.size(); i += 4) {
                const int alpha = f->rgba[i + 3];
                for (size_t k = 0; k < 3; ++k) f->rgba[i + k] = uint8_t((f->rgba[i + k] * alpha + kPaper * (255 - alpha)) / 255);
                f->rgba[i + 3] = 255;
            }
            frame = std::move(f);
        } else {
            JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << error;
        }
        break;
    }
    if (!frame) JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << "the illustration " << fileName << " was not found beside " << exe.string();
    return read[fileName] = frame;
}

} // inline namespace jf
