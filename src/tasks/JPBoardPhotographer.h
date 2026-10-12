// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPJobMachine.h"
#include "JPPhotoStitch.h"

#include "model/JPBoardPhoto.h"
#include "model/JPPlacementsHolderLocation.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// A board's photo (JPBoardPhoto) taken with the head camera: the board's area (its outline; with no size, its
// placements' spread and a margin) covered by pictures a quarter overlapping, taken row by row (each row the other
// way from the last), at the height of the board's surface; then stitched (JPPhotoStitch) at the camera's own scale
// (coarser where that would make too large a photo) and kept beside the board's file. A picture the camera cannot
// reach is left out, and said. One picture is taken a step, so a job can pause or stop between them. What the model
// holds is read only through `onMain`.
class JPBoardPhotographer {
public:
    using OnMain = std::function<void(const std::function<void()>&)>;

    static constexpr double kOverlap = 0.25;          // of a picture, shared with the next
    static constexpr double kMarginMm = 1.0;          // photographed past the board's edge
    static constexpr double kSpreadMarginMm = 2.0;    // round its placements, when it has no size
    static constexpr double kMaxPixels = 25e6;        // a photo at most

    JPBoardPhotographer(JPJobMachine& machine, OnMain onMain);

    // Ready to photograph `location` (a board in the job), its pictures planned. False (why) when it cannot be: the
    // board has no file to keep it beside, no size and no placements, or the camera is not calibrated.
    bool begin(JPPlacementsHolderLocation& location, JPBoardPhoto::Kind kind, const std::string& job, const std::string& run,
               std::string& why);
    // The next picture taken. False (why) when it could not be.
    bool takeNext(std::string& why);
    bool done() const { return m_next >= m_at.size(); }
    int  taken() const { return int(m_next); }
    int  count() const { return int(m_at.size()); }
    const std::string& boardName() const { return m_name; }
    // The photo stitched and kept: `photo` what it is (its notes said in the log too).
    bool finish(JPBoardPhoto& photo, std::string& why);

    // All of it at once (asked for): `goOn` told of each picture taken, false to stop (why: "Stopped").
    static bool photograph(JPJobMachine& machine, const OnMain& onMain, JPPlacementsHolderLocation& location,
                           JPBoardPhoto::Kind kind, const std::string& job, const std::string& run,
                           const std::function<bool(int taken, int count)>& goOn, JPBoardPhoto& photo, std::string& why);

private:
    JPJobMachine&                     m_machine;
    OnMain                            m_onMain;
    JPBoardPhoto                      m_photo;
    std::string                       m_folder, m_name;
    JPAffineTransform                 m_boardToMachine;
    JPPhotoStitch::Area               m_area;
    double                            m_z = 0, m_mmPerPixel = 0;
    std::vector<JPLocation>           m_at;   // where each picture is taken
    size_t                            m_next = 0;
    int                               m_unreachable = 0;
    std::vector<JPPhotoStitch::Tile>  m_tiles;
};

} // inline namespace jf
