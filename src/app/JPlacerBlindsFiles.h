// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

inline namespace jf {

// OpenPnP's Extract 3D-Printing Files (a blinds feeder's Feeder Array): its
// two OpenSCAD files (shipped beside the executable, openscad/) written to a
// folder chosen, never over a file already there, then opened for editing.
class JPlacerBlindsFiles {
public:
    static void extract();
};

} // inline namespace jf
