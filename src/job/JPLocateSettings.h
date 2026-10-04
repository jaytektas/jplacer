// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

inline namespace jf {

// How a job's board is located, the person's choice for the job:
//
//   capture: Automatic, the camera visits each reference and vision finds it;
//            Manual, the positions recorded by hand are used.
//   newBoard (manual capture): ReRecord, a new board on the bed has its
//            references recorded again (each a small jog from where the last
//            board's were), the usual case; Reuse, the recorded positions are
//            used as they are, for a fixture that puts every board in the
//            same place.
struct JPLocateSettings {
    enum class Capture { Automatic, Manual };
    enum class NewBoard { ReRecord, Reuse };

    Capture  capture = Capture::Automatic;
    NewBoard newBoard = NewBoard::ReRecord;
};

} // inline namespace jf
