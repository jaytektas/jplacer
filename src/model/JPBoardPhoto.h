// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSide.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// A photo of a board (JPBoardPhotographer): a PNG in the board's coordinates (its columns along the board's X, its rows
// down its Y, from `originX`, `originY` at the top-left pixel's centre, `mmPerPixel` a pixel), and beside it, of the
// same name, what it is: when it was taken and why (a job starting, a job ending, asked for), the job and its run,
// the board's place in the job, the side that was up, and what was said of how its pictures lined up. A board's
// photos are kept beside its file, in a folder of its name ("board.jpboard.photos"), every one kept until deleted.
class JPBoardPhoto {
public:
    enum class Kind { JobStart, JobEnd, Asked };
    static constexpr const char* kFolderSuffix = ".photos";

    std::string              file;   // the PNG's full path
    Kind                     kind = Kind::Asked;
    std::string              taken;  // as JPWhen keeps it
    std::string              job, run, boardLocation;
    JPSide                   side = JPSide::Top;
    double                   mmPerPixel = 0, originX = 0, originY = 0;
    int                      width = 0, height = 0;
    std::vector<std::string> notes;

    // "Job start", "Job end", "Asked for".
    static const char* kindName(Kind k);
    // As it is listed: "2026-10-12 14:05:31 · Job end · Brd1".
    std::string label() const;
    // The folder of a board's photos (`boardFile` its file).
    static std::string folderOf(const std::string& boardFile);
    // A new photo's PNG in that folder, named for when it was taken, where and why.
    std::string fileIn(const std::string& folder) const;
    // A board's photos, the newest first (none when it has no file or no photos).
    static std::vector<JPBoardPhoto> of(const std::string& boardFile);
    // What it is written beside its PNG (once the PNG is written; the folder made).
    bool saveRecord(std::string& why) const;
    // The PNG and what it is, gone.
    bool remove(std::string& why) const;

    JJson toJson() const;
    // `file`: the PNG it is of.
    static JPBoardPhoto fromJson(const JJson& j, const std::string& file);
    // The record beside a PNG.
    static std::string recordOf(const std::string& png);
};

} // inline namespace jf
