// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>

inline namespace jf {

// The SoundSignaler's two sounds, played through the desktop's sound player
// (pw-play, paplay or aplay, the first there is) without waiting for them.
// As OpenPnP: a file of the same name in the configuration folder's
// "sounds" folder (sounds/error.wav, sounds/success.wav) is played in place
// of jplacer's own.
class JPlacerSound {
public:
    enum class Sound { Error, Success };
    static void play(Sound sound);
    // The file played for `sound`: the configuration folder's, else jplacer's
    // own, written to the temporary folder the first time. Empty when it cannot be written.
    static std::string file(Sound sound);
    // jplacer's own sound: a WAV file's bytes (mono, 16 bit).
    static std::string wav(Sound sound);
};

} // inline namespace jf
