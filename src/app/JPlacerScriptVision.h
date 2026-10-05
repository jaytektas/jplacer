// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once


#include <j/config/Json.h>

#include <opencv2/core.hpp>

#include <mutex>
#include <string>

inline namespace jf {

class JPlacerJobMachine;

// OpenPnP's CvPipeline for scripts: a pipeline (OpenPnP's XML) run on the
// camera the script set (else the head camera) where it is, each stage's result given
// back as OpenPnP's models are (key points, rotated rects, circles, a point,
// a number, a text; and each one as OpenPnP's editor writes it); and the
// last working image shown on the camera (OpenPnP's showFilteredImage).
class JPlacerScriptVision {
public:
    // {"xml": "<cv-pipeline>…", "camera": id or name (none: the head camera)}:
    // {"results": {stage: {"kind", "text", "value"}}}; false and why when it fails.
    bool run(JPlacerJobMachine& machine, const JJson& request, JJson& result, std::string& why);
    // The last run's working image on its camera for `ms`, with `text`.
    bool show(JPlacerJobMachine& machine, int ms, const std::string& text, std::string& why);

private:
    std::mutex m_mutex;
    cv::Mat     m_last;         // BGR
    std::string m_lastCamera;   // where it was taken (empty: the head camera)
};

} // inline namespace jf
