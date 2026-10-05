// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's changer slot vision: a template cut from the middle of a picture
// of the slot is found again in a later picture, the slot moved, by how far
// it moved; the empty slot's template scores better on an empty slot than the
// occupied one's does.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPChangerSlotVision.h"

#include <opencv2/imgproc.hpp>

#include <cmath>

using namespace jf;

namespace {

// A slot (a ring) at (cx, cy) on a grey plate; occupied, a tip (a dark disc) in it.
cv::Mat slot(double cx, double cy, bool occupied) {
    cv::Mat m(480, 640, CV_8UC3, cv::Scalar::all(120));
    cv::circle(m, cv::Point(int(std::lround(cx)), int(std::lround(cy))), 40, cv::Scalar::all(230), 6, cv::LINE_AA);
    cv::rectangle(m, cv::Rect(int(cx) - 70, int(cy) + 50, 140, 12), cv::Scalar::all(40), cv::FILLED);
    if (occupied) cv::circle(m, cv::Point(int(std::lround(cx)), int(std::lround(cy))), 22, cv::Scalar::all(30), cv::FILLED);
    return m;
}

} // namespace

int main() {
    assert(JPChangerSlotVision::evenPixels(10, 0.1) == 100 && JPChangerSlotVision::evenPixels(10.05, 0.1) == 100
           && JPChangerSlotVision::evenPixels(10.15, 0.1) == 102);
    // Templates: 10 mm square at 0.1 mm a pixel, from pictures of the slot in the middle.
    const int t = JPChangerSlotVision::evenPixels(10, 0.1), searched = JPChangerSlotVision::evenPixels(10 + 4, 0.1);
    const cv::Mat emptyPicture = slot(320, 240, false), occupiedPicture = slot(320, 240, true);
    const cv::Mat empty = emptyPicture(JPChangerSlotVision::middle(emptyPicture, t, t)).clone();
    const cv::Mat occupied = occupiedPicture(JPChangerSlotVision::middle(occupiedPicture, t, t)).clone();
    assert(empty.cols == 100 && empty.rows == 100);

    // The slot 7 pixels right and 5 up of the middle: found there.
    const cv::Mat later = slot(327, 235, false);
    JPChangerSlotVision::Match e, o;
    std::string why;
    assert(JPChangerSlotVision::find(later, empty, searched, searched, e, why));
    assert(JPChangerSlotVision::find(later, occupied, searched, searched, o, why));
    assert(std::abs(e.dxPx - 7) < 1 && std::abs(e.dyPx + 5) < 1 && e.score > 0.9);
    // Empty: the empty template the better.
    assert(e.score > o.score);
    // Occupied: the occupied template the better.
    const cv::Mat loaded = slot(316, 244, true);
    assert(JPChangerSlotVision::find(loaded, empty, searched, searched, e, why));
    assert(JPChangerSlotVision::find(loaded, occupied, searched, searched, o, why));
    assert(o.score > e.score && std::abs(o.dxPx + 4) < 1 && std::abs(o.dyPx - 4) < 1);
    // A grey template on a colour picture: made alike first.
    cv::Mat grey;
    cv::cvtColor(empty, grey, cv::COLOR_BGR2GRAY);
    assert(JPChangerSlotVision::find(later, grey, searched, searched, e, why) && std::abs(e.dxPx - 7) < 1);
    // Too large a template for the search: said.
    assert(!JPChangerSlotVision::find(later, empty, 60, 60, e, why) && !why.empty());
    return 0;
}
