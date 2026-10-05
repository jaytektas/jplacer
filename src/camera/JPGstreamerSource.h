// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCaptureSource.h"

#include <sys/types.h>

#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's GstreamerCamera: pictures from any GStreamer pipeline (as
// gst-launch writes one: "v4l2src device=/dev/video0 ! videoconvert"), run
// by the system's GStreamer (gst-launch-1.0) with its output turned to RGBA
// and handed over raw. Its size and rate are what the pipeline gives.
class JPGstreamerSource : public JPCaptureSource {
public:
    JPGstreamerSource(std::string name, std::string pipeline);
    ~JPGstreamerSource() override { close(); }

    bool open(std::string& error) override;
    void close() override;
    std::vector<JPCaptureMode> modes() const override;
    bool start(const JPCaptureMode&, std::string&) override { return true; }
    bool grab(JPFrame& frame, int timeoutMs, std::string& error) override;
    std::string describe() const override { return m_name + " (GStreamer: " + m_pipeline + ")"; }

    // From gst-launch's verbose line for its caps ("... caps = video/x-raw,
    // format=(string)RGBA, width=(int)640, height=(int)480, framerate=(fraction)30/1"):
    // the size and rate; false when the line is not one.
    static bool caps(const std::string& line, int& width, int& height, double& fps);
    // A pipeline in gst-launch's words, as a shell would split it (on spaces,
    // a "quoted" part kept whole).
    static std::vector<std::string> words(const std::string& pipeline);

private:
    // What gst-launch has said since, to the log; false (with its error, or
    // that it ended) once it has stopped.
    bool listen(std::string& error);

    std::string m_name, m_pipeline;
    pid_t       m_pid = -1;
    int         m_frames = -1, m_messages = -1;
    std::string m_said;
    int         m_width = 0, m_height = 0;
    double      m_fps = 0;
};

} // inline namespace jf
