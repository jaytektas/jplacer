// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstdint>
#include <string>

inline namespace jf {

class JPPipeline;

// Vision debugging (Preferences: Save vision pictures for debugging), what OpenPnP does at its Debug log level:
// while on, every pipeline run keeps each stage's picture, in a folder of its own under log/vision in the
// directory given (the configuration's, as OpenPnP's): "<when>_<what>/NN_<stage>.png", with stages.txt saying
// each stage's class, result and time; and the pipelines' ImageWriteDebug stages write, as OpenPnP's
// createResourceFile, into "org.openpnp.vision.pipeline.stages.ImageWriteDebug" there. From any thread.
// Rolling: all it has written (both places) kept under a limit, the oldest let go first (OpenPnP keeps them all, and
// a day of it filled 11 GB).
class JPVisionDebug {
public:
    // Where the pictures go; empty: off.
    static void        setDirectory(const std::string& directory);
    static bool        on();
    static std::string directory();
    // ImageWriteDebug's folder (empty while off).
    static std::string imageWriteDebugDirectory();
    // A run of `pipeline` kept, `what` naming what it was for ("" : "pipeline").
    static void saveRun(const JPPipeline& pipeline, const std::string& what);
    // A picture written into imageWriteDebugDirectory(), counted against the limit.
    static void wrote(const std::string& path);
    static constexpr std::uintmax_t kBytesPerMb = 1024 * 1024;
    // How much it may keep, in bytes; 0: no limit. Lowered: the oldest let go now.
    static void          setLimit(std::uintmax_t bytes);
    static std::uintmax_t limit();
    // What it holds now (counted once from the disk, then kept as it writes and lets go).
    static std::uintmax_t held();
};

} // inline namespace jf
