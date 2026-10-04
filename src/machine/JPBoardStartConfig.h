// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/config/Json.h>

#include <algorithm>
#include <string>
#include <utility>

inline namespace jf {

// How the machine gets its first guess of where a board is, before the
// camera looks for its references. It depends on how boards are held, so it
// is the machine's:
//
//   ByHand:  the person jogs the camera onto one reference and says which;
//   Anchor:  a place on the machine boards are mounted at (a clamp, a corner
//            stop): one corner of the board sits at (anchorX, anchorY), the
//            board turned as the side up says; within a millimetre or two,
//            which the search takes up;
//   Region:  the camera scans a region for the first reference (a round
//            mark): the rectangle given, or with none the whole travel of
//            the head's X and Y within their soft limits.
struct JPBoardStartConfig {
    enum class Kind { ByHand, Anchor, Region };
    enum class Corner { BottomLeft, BottomRight, TopLeft, TopRight };
    // For each side up: which of the board's corners sits at the anchor, and
    // how far the board is turned on the machine (0, 90, 180 or 270).
    struct Side {
        Corner corner = Corner::BottomLeft;
        double turnDeg = 0;
    };

    Kind   kind = Kind::ByHand;
    double anchorX = 0, anchorY = 0;
    Side   top, bottom;
    double regionX0 = 0, regionY0 = 0, regionX1 = 0, regionY1 = 0;   // all 0: the whole travel

    bool hasRegion() const { return regionX1 > regionX0 && regionY1 > regionY0; }

    static const char* cornerName(Corner c) {
        switch (c) {
            case Corner::BottomLeft:  return "Bottom left";
            case Corner::BottomRight: return "Bottom right";
            case Corner::TopLeft:     return "Top left";
            case Corner::TopRight:    return "Top right";
        }
        return "";
    }

    static JPBoardStartConfig fromJson(const JJson& j) {
        JPBoardStartConfig c;
        const std::string k = j["kind"].str();
        c.kind = k == "anchor" ? Kind::Anchor : k == "region" ? Kind::Region : Kind::ByHand;
        c.anchorX = j["anchorX"].number();
        c.anchorY = j["anchorY"].number();
        for (const auto& [key, side] : { std::pair{ "top", &c.top }, std::pair{ "bottom", &c.bottom } }) {
            side->corner = Corner(std::clamp(int(j[key]["corner"].number()), 0, 3));
            side->turnDeg = j[key]["turn"].number();
        }
        c.regionX0 = j["region"]["x0"].number();
        c.regionY0 = j["region"]["y0"].number();
        c.regionX1 = j["region"]["x1"].number();
        c.regionY1 = j["region"]["y1"].number();
        return c;
    }
    JJson toJson() const {
        JJson j = JJson::object();
        j["kind"] = kind == Kind::Anchor ? "anchor" : kind == Kind::Region ? "region" : "hand";
        j["anchorX"] = anchorX;
        j["anchorY"] = anchorY;
        for (const auto& [key, side] : { std::pair{ "top", &top }, std::pair{ "bottom", &bottom } }) {
            JJson s = JJson::object();
            s["corner"] = int(side->corner);
            s["turn"] = side->turnDeg;
            j[key] = std::move(s);
        }
        JJson r = JJson::object();
        r["x0"] = regionX0;
        r["y0"] = regionY0;
        r["x1"] = regionX1;
        r["y1"] = regionY1;
        j["region"] = std::move(r);
        return j;
    }
};

} // inline namespace jf
