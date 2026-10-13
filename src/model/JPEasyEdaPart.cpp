// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPEasyEdaPart.h"

#include <j/config/Json.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <regex>
#include <sstream>

inline namespace jf {

namespace {

// A pad's fields (EasyEDA's "PAD~shape~x~y~width~height~layer~net~number~hole~points~rotation~…").
enum Field { kShape = 1, kX, kY, kWidth, kHeight, kLayer, kNet, kNumber, kHole, kPoints, kRotation };
constexpr const char* kTopCopper = "1";

std::vector<std::string> split(const std::string& s, char by) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == by) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

double num(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

} // namespace

std::string JPEasyEdaPart::partNumber(const std::string& typed) {
    std::string s;
    for (const char c : typed)
        if (!std::isspace(uint8_t(c))) s += char(std::toupper(uint8_t(c)));
    if (!s.empty() && s[0] == 'C') s.erase(0, 1);
    if (s.empty() || !std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(uint8_t(c)); })) return "";
    return "C" + s;
}

std::string JPEasyEdaPart::url(const std::string& lcsc) {
    return "https://easyeda.com/api/products/" + lcsc + "/components?version=6.4.19.5";
}

std::string JPEasyEdaPart::modelUrl(const std::string& uuid) {
    return "https://modules.easyeda.com/3dmodel/" + uuid;
}

bool JPEasyEdaPart::parse(const std::string& json, JPEasyEdaPart& out, std::string& why) {
    out = JPEasyEdaPart();
    const std::optional<JJson> parsed = JJson::tryParse(json);
    if (!parsed || !parsed->isObject()) {
        why = "EasyEDA's answer could not be read";
        return false;
    }
    const JJson& d = *parsed;
    const JJson& r = d["result"];
    if (!d["success"].isBool() || !d["success"].boolean() || !r.isObject()) {
        why = "EasyEDA knows no such part";
        return false;
    }
    const JJson& para = r["dataStr"]["head"]["c_para"];
    out.lcsc = para["Supplier Part"].str();
    out.mpn = para["Manufacturer Part"].str();
    if (out.mpn.empty()) out.mpn = r["title"].str();
    out.manufacturer = para["Manufacturer"].str();
    out.description = r["description"].str();
    const JJson& pkg = r["packageDetail"];
    const JJson& data = pkg["dataStr"];
    out.footprintName = pkg["title"].str();
    // Its height, from its 3D model's name ("C0603_L1.6-W0.8-H0.8").
    static const std::regex height(R"([_-]H(\d+(?:\.\d+)?)(?:$|[-_]))");
    std::smatch m;
    const std::string model = data["head"]["c_para"]["3DModel"].str();
    if (std::regex_search(model, m, height)) out.heightMm = num(m[1].str());
    const double ox = data["head"]["x"].isNumber() ? data["head"]["x"].number() : 0;
    const double oy = data["head"]["y"].isNumber() ? data["head"]["y"].number() : 0;
    for (size_t i = 0; i < data["shape"].size(); ++i) {
        const std::string shape = data["shape"][i].str();
        // The 3D model: "SVGNODE~{…}", its attributes the model's id, the box it covers and how it is turned.
        if (shape.rfind("SVGNODE~", 0) == 0) {
            const std::optional<JJson> node = JJson::tryParse(shape.substr(8));
            if (!node || !node->isObject()) continue;
            const JJson& at = (*node)["attrs"];
            if (at["c_etype"].str() != "outline3D" || at["uuid"].str().empty()) continue;
            Model m;
            m.uuid = at["uuid"].str();
            const std::vector<std::string> turn = split(at["c_rotation"].str(), ',');
            if (turn.size() == 3) {
                m.rotationX = num(turn[0]);
                m.rotationY = num(turn[1]);
                m.rotationZ = num(turn[2]);
            }
            // The box it covers as placed: round the outline drawn of it. Its c_width and c_height are the model's
            // own, before it is turned (an 0603 resistor's model turned a quarter about Z lies across its pads).
            double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
            for (size_t c = 0; c < (*node)["childNodes"].size(); ++c) {
                std::istringstream pts((*node)["childNodes"][c]["attrs"]["points"].str());
                double x = 0, y = 0;
                while (pts >> x >> y) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
            if (maxX > minX && maxY > minY) {
                m.widthMm = (maxX - minX) * kUnitMm;
                m.lengthMm = (maxY - minY) * kUnitMm;
            } else {
                const bool across = std::lround(m.rotationZ / 90.0) % 2 != 0;
                m.widthMm = num(at[across ? "c_height" : "c_width"].str()) * kUnitMm;
                m.lengthMm = num(at[across ? "c_width" : "c_height"].str()) * kUnitMm;
            }
            out.model = m;
            continue;
        }
        if (shape.rfind("PAD~", 0) != 0) continue;
        const std::vector<std::string> f = split(shape, '~');
        if (f.size() <= kRotation) continue;
        if (f[kLayer] != kTopCopper) {
            ++out.throughHole;
            continue;
        }
        JPFootprint::Pad p;
        p.name = f[kNumber];
        p.x = (num(f[kX]) - ox) * kUnitMm;
        p.y = (oy - num(f[kY])) * kUnitMm;
        p.width = num(f[kWidth]) * kUnitMm;
        p.height = num(f[kHeight]) * kUnitMm;
        // EasyEDA turns anticlockwise as seen, Y down: the same turn with Y up.
        p.rotation = std::fmod(num(f[kRotation]), 360.0);
        const std::string& kind = f[kShape];
        if (kind == "OVAL" || kind == "ELLIPSE") {
            p.roundness = 100;
        } else if (kind == "POLYGON") {
            // The rectangle round its points (machine-straight), unturned.
            std::istringstream pts(f[kPoints]);
            double x = 0, y = 0, minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
            while (pts >> x >> y) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
            if (maxX > minX && maxY > minY) {
                p.x = ((minX + maxX) / 2 - ox) * kUnitMm;
                p.y = (oy - (minY + maxY) / 2) * kUnitMm;
                p.width = (maxX - minX) * kUnitMm;
                p.height = (maxY - minY) * kUnitMm;
                p.rotation = 0;
            }
            ++out.polygons;
        }
        p.mark = p.name == "1";
        out.pads.push_back(p);
    }
    if (out.pads.empty()) {
        why = out.throughHole > 0 ? "its footprint has only through-hole pads" : "EasyEDA has no footprint for it";
        return false;
    }
    if (out.lcsc.empty()) out.lcsc = r["lcsc"]["number"].str();
    return true;
}

} // inline namespace jf
