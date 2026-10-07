// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

class JPPipeline;

// Vision debugging (Preferences: Save vision pictures for debugging), what OpenPnP does at its Debug log level:
// while on, every pipeline run keeps each stage's picture, in a folder of its own under log/vision in the
// directory given (the configuration's, as OpenPnP's): "<when>_<what>/NN_<stage>.png", with stages.txt saying
// each stage's class, result and time; and the pipelines' ImageWriteDebug stages write, as OpenPnP's
// createResourceFile, into "org.openpnp.vision.pipeline.stages.ImageWriteDebug" there. From any thread.
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
};

} // inline namespace jf
