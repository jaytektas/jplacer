// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

inline namespace jf {

// Help ▸ About jplacer: the framework's message dialog with a picture, as
// jayecu Studio's About box is: the Jaytek logo (drawn at its own size, one
// pixel for each, so it stays sharp), then jplacer's name, what it is, its
// version, copyright and licence, where it lives, OpenPnP's part in it (its
// copyright and licence; NOTICE.md has the whole of it), and what it is
// built on.
class JPlacerAbout {
public:
    static void show();
};

} // inline namespace jf
