// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's GstreamerCamera: a pipeline's pictures, through the system's
// GStreamer, at the size and rate the pipeline gives; a pipeline GStreamer
// cannot run fails saying why.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPGstreamerSource.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace jf;

int main() {
    // gst-launch's verbose caps line.
    {
        int w = 0, h = 0;
        double fps = 0;
        assert(JPGstreamerSource::caps("/GstPipeline:pipeline0/GstCapsFilter:jplacercaps.GstPad:src: caps = video/x-raw, "
                                       "format=(string)RGBA, width=(int)640, height=(int)480, framerate=(fraction)30/1",
                                       w, h, fps));
        assert(w == 640 && h == 480 && fps == 30);
        assert(!JPGstreamerSource::caps("/GstPipeline:pipeline0/GstVideoTestSrc:videotestsrc0.GstPad:src: caps = video/x-raw, "
                                        "width=(int)64, height=(int)48", w, h, fps));
    }
    // Split as a shell would.
    {
        const auto w = JPGstreamerSource::words("v4l2src  device=/dev/video0 ! capsfilter caps=\"video/x-raw, width=640\" !");
        assert(w.size() == 6 && w[1] == "device=/dev/video0" && w[4] == "caps=video/x-raw, width=640" && w[5] == "!");
    }
    if (std::system("gst-launch-1.0 --version >/dev/null 2>&1") != 0) {
        std::printf("GStreamer is not installed here: its pictures not tried\n");
        return 0;
    }
    {
        JPGstreamerSource src("Test", "videotestsrc pattern=white ! video/x-raw,width=64,height=48,framerate=30/1");
        std::string why;
        const bool ok = src.open(why);
        if (!ok) std::fprintf(stderr, "why: %s\n", why.c_str());
        assert(ok);
        assert(src.modes().front().width == 64 && src.modes().front().height == 48 && src.modes().front().fps == 30);
        JPFrame f;
        for (int i = 0; i < 3; ++i) {
            assert(src.grab(f, 3000, why));
            assert(f.width == 64 && f.height == 48 && f.rgba.size() == 64 * 48 * 4);
        }
        assert(f.rgba[0] > 200 && f.rgba[1] > 200 && f.rgba[2] > 200 && f.rgba[3] == 255);   // white, opaque
        // At its rate (a test pattern is not live: paced by the clock), not as fast as it can make them.
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) assert(src.grab(f, 3000, why));
        assert(std::chrono::steady_clock::now() - t0 > std::chrono::milliseconds(250));
        src.close();
    }
    {
        JPGstreamerSource src("Wrong", "nosuchelement");
        std::string why;
        assert(!src.open(why) && !why.empty());
    }
    return 0;
}
