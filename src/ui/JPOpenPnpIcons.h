// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/graphics/GpuHal.h>

#include <map>
#include <string>
#include <tuple>

inline namespace jf {

// OpenPnP's icons (the SVG files shipped in icons/, as OpenPnP's Icons
// names them: "general-add", "pick", "capture-camera"…), drawn to the size
// a button asks for and kept as textures. Under a dark theme an icon's
// "_dark" file is used where there is one, as OpenPnP's look does.
//
// One lives for as long as the window's graphics; buttons find it through
// instance().
class JPOpenPnpIcons {
public:
    explicit JPOpenPnpIcons(JGpuHal& hal);
    ~JPOpenPnpIcons();
    JPOpenPnpIcons(const JPOpenPnpIcons&) = delete;
    JPOpenPnpIcons& operator=(const JPOpenPnpIcons&) = delete;

    static JPOpenPnpIcons* instance();

    // `name` drawn `pixels` square; kNullTexture when there is no such icon.
    // `disabled`: greyed, as Swing greys a disabled button's icon.
    TextureHandle texture(const std::string& name, int pixels, bool disabled = false);
    // Whether the theme in use is a dark one.
    static bool darkTheme();

private:
    std::string file(const std::string& name, bool dark) const;

    JGpuHal&                                                    m_hal;
    std::string                                                 m_dir;
    std::map<std::tuple<std::string, int, bool, bool>, TextureHandle> m_textures;
};

} // inline namespace jf
