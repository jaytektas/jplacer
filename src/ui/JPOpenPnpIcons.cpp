// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOpenPnpIcons.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"

#include <j/core/JStyle.h>
#include <j/core/Log.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

inline namespace jf {

namespace fs = std::filesystem;

namespace {

JPOpenPnpIcons* s_instance = nullptr;
// SVG lengths are read at this many dots an inch, as the files are drawn for.
constexpr float kSvgDpi = 96.f;
// A disabled icon's grey (a share of the brightened grey) and how much of it shows.
constexpr float kGreyShade = 0.8f, kGreyAlpha = 0.6f;

} // namespace

JPOpenPnpIcons::JPOpenPnpIcons(JGpuHal& hal) : m_hal(hal) {
    const fs::path exe = JPlacerPaths::exeDir();
    for (const fs::path& d : { exe / "icons", exe / ".." / "icons" })   // shipped beside it; build/ -> icons/
        if (fs::exists(d / "general-add.svg")) {
            m_dir = d.string();
            break;
        }
    if (m_dir.empty()) JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << "OpenPnP's icons were not found beside " << exe.string();
    s_instance = this;
}

JPOpenPnpIcons::~JPOpenPnpIcons() {
    for (const auto& [key, tex] : m_textures)
        if (tex != kNullTexture) m_hal.releaseTexture(tex);
    if (s_instance == this) s_instance = nullptr;
}

JPOpenPnpIcons* JPOpenPnpIcons::instance() {
    return s_instance;
}

bool JPOpenPnpIcons::darkTheme() {
    const uint8_t* s = Colors::Surface1;
    return (int(s[0]) * 299 + int(s[1]) * 587 + int(s[2]) * 114) / 1000 < 128;
}

std::string JPOpenPnpIcons::file(const std::string& name, bool dark) const {
    if (m_dir.empty()) return {};
    if (dark) {
        const fs::path d = fs::path(m_dir) / (name + "_dark.svg");
        if (fs::exists(d)) return d.string();
    }
    const fs::path p = fs::path(m_dir) / (name + ".svg");
    return fs::exists(p) ? p.string() : std::string();
}

TextureHandle JPOpenPnpIcons::texture(const std::string& name, int pixels, bool disabled) {
    if (pixels <= 0) return kNullTexture;
    const bool dark = darkTheme();
    const auto key = std::make_tuple(name, pixels, dark, disabled);
    if (const auto it = m_textures.find(key); it != m_textures.end()) return it->second;
    TextureHandle tex = kNullTexture;
    const std::string path = file(name, dark);
    if (NSVGimage* image = path.empty() ? nullptr : nsvgParseFromFile(path.c_str(), "px", kSvgDpi)) {
        if (NSVGrasterizer* r = nsvgCreateRasterizer()) {
            const float side = std::max(image->width, image->height);
            const float scale = side > 0 ? float(pixels) / side : 1.f;
            std::vector<unsigned char> rgba(size_t(pixels) * size_t(pixels) * 4, 0);
            // Centred, as an icon smaller one way is drawn.
            const float tx = (float(pixels) - image->width * scale) * 0.5f;
            const float ty = (float(pixels) - image->height * scale) * 0.5f;
            nsvgRasterize(r, image, tx, ty, scale, rgba.data(), pixels, pixels, pixels * 4);
            if (disabled)
                for (size_t i = 0; i < rgba.size(); i += 4) {
                    // Swing's GrayFilter: grey, brightened halfway, half seen.
                    const int lum = (rgba[i] * 299 + rgba[i + 1] * 587 + rgba[i + 2] * 114) / 1000;
                    const unsigned char g = static_cast<unsigned char>((255 - (255 - lum) / 2) * kGreyShade);
                    rgba[i] = rgba[i + 1] = rgba[i + 2] = g;
                    rgba[i + 3] = static_cast<unsigned char>(rgba[i + 3] * kGreyAlpha);
                }
            tex = m_hal.uploadTexture(rgba.data(), uint32_t(pixels), uint32_t(pixels));
            nsvgDeleteRasterizer(r);
        }
        nsvgDelete(image);
    } else if (!path.empty()) {
        JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << path << ": not an icon that can be read";
    }
    m_textures[key] = tex;
    return tex;
}

} // inline namespace jf
