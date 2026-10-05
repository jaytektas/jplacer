// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "tasks/JPJobMachine.h"

#include <j/config/Json.h>

#include <opencv2/core.hpp>

#include <mutex>
#include <string>

inline namespace jf {

// OpenPnP's CvPipeline for scripts: a pipeline (OpenPnP's XML) run on the head
// camera where it is (JPJobMachine::lookThrough), each stage's result given
// back as OpenPnP's models are (key points, rotated rects, circles, a point,
// a number, a text; and each one as OpenPnP's editor writes it); and the
// last working image shown on the camera (OpenPnP's showFilteredImage).
class JPlacerScriptVision {
public:
    // {"xml": "<cv-pipeline>…"}: {"results": {stage: {"kind", "text", "value"}}}; false and why when it fails.
    bool run(JPJobMachine& machine, const JJson& request, JJson& result, std::string& why);
    // The last run's working image on the camera for `ms`.
    bool show(JPJobMachine& machine, int ms, std::string& why);

private:
    std::mutex m_mutex;
    cv::Mat    m_last;   // BGR
};

} // inline namespace jf
