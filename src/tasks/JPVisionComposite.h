// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPFootprint.h"
#include "model/JPVisionCompositing.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's VisionCompositing.Composite: how bottom vision sees a part
// bigger than the camera's view (or its roaming radius allows) in several
// shots, each of a few of its corners, and puts what the shots saw
// together into one centre, angle and size.
//
// From the package's footprint (or body, with the Body method) the pads'
// outer edges give candidate corners; each corner the nozzle can bring
// within the camera's roaming radius, and whose view holds enough of its
// edges and nothing else, is eligible. Eligible corners pair up
// (diagonal, aligned, mirrored, symmetric) into the best solution OpenPnP
// knows (Square, Box, Z, Arrow, Figure7, Angle, Trapezoid), and the corners
// near enough each other share a shot. A part that fits the view is one
// shot (Small). All in the footprint's frame, mm, part at 0°.
class JPVisionComposite {
public:
    enum class Solution { Square, Box, Z, Arrow, Figure7, Angle, Trapezoid, Small, VisionOffsets, NoFootprint,
                          NoCameraRoaming, RestrictedCameraRoaming, Invalid };
    enum class ShotConfiguration { Square, Box, MirrorX, MirrorY, Corner, Unknown };
    static const char* solutionName(Solution s);
    static const char* configurationName(ShotConfiguration c);
    // Solved from corners (several shots put together); else one shot.
    static bool isAdvanced(Solution s) { return int(s) < int(Solution::Small); }
    static bool isInvalid(Solution s) { return int(s) >= int(Solution::VisionOffsets); }

    struct Corner;
    struct Shot {
        std::vector<const Corner*> corners;   // none: the whole part
        double x = 0, y = 0;                  // the shot's middle
        double width = 0, height = 0;
        double minMaskRadius = 0, maxMaskRadius = 0;
        bool   optional = false;
        ShotConfiguration configuration = ShotConfiguration::Unknown;
        bool hasLeftEdge() const;
        bool hasRightEdge() const;
        bool hasTopEdge() const;
        bool hasBottomEdge() const;
    };
    struct Corner {
        double x = 0, y = 0;
        double minMaskRadius = 0, maxMaskRadius = 0;
        int    xSign = 0, ySign = 0;
        bool   optional = false;
        int    rating = 0;
        bool   square = false;
        Solution solution = Solution::Invalid;
        Corner* diagonalBuddy = nullptr;
        Corner* xAlignedBuddy = nullptr;
        Corner* yAlignedBuddy = nullptr;
        Corner* xSymmetricBuddy = nullptr;
        Corner* ySymmetricBuddy = nullptr;
        Corner* xMirrorBuddy = nullptr;
        Corner* yMirrorBuddy = nullptr;
    };

    // What the composite is worked out from.
    struct Input {
        std::string         packageId;
        JPFootprint         footprint;            // in mm
        JPVisionCompositing compositing;
        double              toleranceMm = 0;      // the nozzle tip's max pick tolerance (the package's own when set)
        double              maxPartDiameterMm = 0;
        double              cameraWidthMm = 0, cameraHeightMm = 0;   // what the camera sees
        double              roamingRadiusMm = 0;  // the camera's; 0: not set
        std::string         cameraName;
        bool                visionOffsets = false;   // the bottom vision settings have Vision Offsets
        std::string         settingsName;
    };
    explicit JPVisionComposite(const Input& in);
    JPVisionComposite(const JPVisionComposite&)            = delete;
    JPVisionComposite& operator=(const JPVisionComposite&) = delete;

    Solution                 solution() const { return m_solution; }
    const std::string&       diagnostics() const { return m_diagnostics; }
    const std::vector<Shot>& shots() const { return m_shots; }
    double                   tolerance() const { return m_tolerance; }
    double                   maxPadRadius() const { return m_maxPadRadius; }
    double                   maxCornerRadius() const { return m_maxCornerRadius; }
    const std::vector<JPFootprint::Pad>& rectifiedPads() const { return m_rectifiedPads; }
    double                   computeSeconds() const { return m_computeSeconds; }

    // The shots to take (each needed one, and as many optional ones as
    // Extra Shots asks for), in the shortest order from where the nozzle
    // is, `fromX`, `fromY` being the nozzle's place relative to the part's
    // centre over the camera, part frame.
    std::vector<const Shot*> travel(double fromX, double fromY) const;

    // A shot's result: the rectangle it found, as its four corners'
    // places in the part's frame relative to the camera centre: upper left,
    // upper right, lower left, lower right ("reading order").
    struct Point { double x = 0, y = 0; };
    void accumulate(const Shot& shot, const std::array<Point, 4>& points);
    // What the shots found forgotten, for another look.
    void restart() { m_cornerMap.clear(); }
    // What the shots found together: the part's centre (part frame, mm,
    // from the camera centre), angle (degrees, nearest `expectedAngle`) and
    // size. False (with why) when they do not give a centre and an angle.
    struct Detected {
        Point  center;
        double angle = 0;
        Point  scale { 1, 1 };
        Point  size;
    };
    bool interpret(double expectedAngle, Detected& out, std::string& why) const;

private:
    void compute(const Input& in);
    void addPadToOctogonalHull(const JPFootprint::Pad& pad);
    std::vector<JPFootprint::Pad> fusedPads(const std::vector<JPFootprint::Pad>& pads) const;
    double requiredRoamingRadius(double x, double y) const;
    void findEligibleCorners(const std::vector<double>& xEdges, const std::vector<double>& yEdges, int xSign, int ySign,
                             double roamingRadius);
    int  computeBuddies(double minLeverage);
    void composeShots(std::vector<Corner*> solution);

    std::vector<std::unique_ptr<Corner>> m_corners;
    std::vector<Shot>                    m_shots;
    Solution                             m_solution = Solution::Invalid;
    std::string                          m_diagnostics;
    JPVisionCompositing::Method          m_method = JPVisionCompositing::Method::Restricted;
    int                                  m_extraShots = 0;
    double                               m_tolerance = 0;
    double                               m_cameraViewRadius = 0;
    double                               m_maxPadRadius = 0;
    double                               m_maxCornerRadius = 0;
    double                               m_octogonalHull[8] {};
    std::vector<double>                  m_leftEdges, m_rightEdges, m_topEdges, m_bottomEdges;
    std::vector<JPFootprint::Pad>        m_rectifiedPads;
    int                                  m_outOfRoamingCandidates = 0;
    double                               m_computeSeconds = 0;
    std::map<const Corner*, Point>       m_cornerMap;
};

} // inline namespace jf
