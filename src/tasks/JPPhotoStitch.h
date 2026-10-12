// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPAffineTransform.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

inline namespace jf {

// One picture of a board made from many (a board's photo, JPBoardPhotographer): each picture placed where the
// machine says it was taken, then lined up with its neighbours by what they both show (phase correlation of their
// overlap, in the machine's coordinates), the corrections that best agree with every pair found together (least
// squares), and the pictures blended across their overlaps (each weighted by how far in from its edges a pixel is).
// The photo is in the board's coordinates: its columns along the board's X, its rows down the board's Y.
class JPPhotoStitch {
public:
    struct Tile {
        cv::Mat           bgr;
        JPAffineTransform toMachine;   // a pixel's place on the machine (mm)
    };
    // The board's area photographed (board mm).
    struct Area {
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
    };
    struct Result {
        cv::Mat bgra;                       // not covered by any picture: transparent
        double  mmPerPixel = 0;
        double  originX = 0, originY = 0;   // the board's point at the top-left pixel's centre
        int     pairs = 0, lined = 0;       // overlaps between pictures, and those lined up by what they show
        double  largestCorrectionMm = 0;    // the most a picture was moved from where the machine said it was
        double  largestDisagreementMm = 0;  // the most a pair still disagrees by, its pictures moved
        double  uncoveredShare = 0;         // of the area, not in any picture
    };

    // A correction past this is said (mm): the machine's place for a picture is that far out.
    static constexpr double kCorrectionToSayMm = 0.1;
    // Pairs that still disagree by more than this after lining up are said (mm).
    static constexpr double kDisagreementToSayMm = 0.05;
    // A share of the area not covered past this is said.
    static constexpr double kUncoveredToSay = 0.005;

    // `boardToMachine`: a board point's place on the machine. False (and why) when no picture can be placed.
    static bool stitch(const std::vector<Tile>& tiles, const JPAffineTransform& boardToMachine, const Area& area,
                       double mmPerPixel, Result& result, std::string& why);

    // Two pictures' overlap compared: how far `b`'s content lies from `a`'s (mm, machine X and Y), and how sure that
    // is (phase correlation's response). False when they do not overlap enough to tell.
    static bool shiftBetween(const Tile& a, const Tile& b, double mmPerPixel, double& dx, double& dy, double& response);

    // Below this response a pair's shift is not taken (what they show is too plain to line up by).
    static constexpr double kMinResponse = 0.05;
    // An overlap whose grey varies less than this (standard deviation, of 255) shows nothing to line up by.
    static constexpr double kMinContrast = 2.0;
    // A shift past this is not taken (mm): not the same thing seen.
    static constexpr double kMaxShiftMm = 1.0;
    // An overlap narrower than this is not compared (pixels).
    static constexpr int kMinOverlapPx = 24;
    // Rows of the photo made at a time.
    static constexpr int kStripRows = 512;
};

} // inline namespace jf
