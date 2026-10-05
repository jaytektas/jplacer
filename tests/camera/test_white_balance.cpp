// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// White balance as OpenPnP does it: a channel scaled by its balance, then its
// gamma; and worked out from a grey picture with a colour cast.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPWhiteBalance.h"

#include <cmath>
#include <cstdio>

using namespace jf;

namespace {

// A picture of one colour, with a few brighter pixels (a grey card, lit).
JPFrame picture(uint8_t r, uint8_t g, uint8_t b) {
    JPFrame f;
    f.width = 10;
    f.height = 10;
    f.sequence = 1;
    for (int i = 0; i < 100; ++i) {
        const double k = i < 90 ? 1.0 : 1.2;
        f.rgba.insert(f.rgba.end(), { uint8_t(r * k), uint8_t(g * k), uint8_t(b * k), 255 });
    }
    return f;
}

} // namespace

int main() {
    // Neutral: nothing changes.
    JPFrame f = picture(100, 120, 80);
    JPWhiteBalance(JPCameraConfig::WhiteBalance{}).apply(f);
    assert(f.rgba[0] == 100 && f.rgba[1] == 120 && f.rgba[2] == 80);

    // A balance scales its channel; a gamma bends it.
    JPCameraConfig::WhiteBalance wb;
    wb.balance = { 1.0, 1.0, 1.5 };
    f = picture(100, 100, 100);
    JPWhiteBalance(wb).apply(f);
    assert(f.rgba[0] == 100 && f.rgba[2] == 150);
    wb = {};
    wb.gamma = { 2.0, 1.0, 1.0 };
    f = picture(64, 64, 64);
    JPWhiteBalance(wb).apply(f);
    assert(std::abs(f.rgba[0] - std::sqrt(64.0 / 255) * 255) <= 1 && f.rgba[1] == 64);

    // A grey with too little blue: worked out, the blue is brought up to match.
    std::string why;
    const auto v = JPWhiteBalance::automatic(picture(160, 160, 120), true, why);
    assert(v && std::abs(v->balance[0] - 1) < 0.01 && std::abs(v->balance[2] - 160.0 / 120.0) < 0.05);
    f = picture(160, 160, 120);
    JPWhiteBalance(*v).apply(f);
    assert(std::abs(int(f.rgba[2]) - int(f.rgba[0])) <= 2);

    // Too dark to tell.
    assert(!JPWhiteBalance::automatic(picture(10, 10, 10), true, why) && !why.empty());

    // Mapped (OpenPnP's Mapped Roughly): a gray gradient whose blue runs at 80%, mapped at 8 levels; the
    // table then gives blue back what red and green have, along the gradient; a manual balance's maps none.
    {
        JPFrame g;
        g.width = 256;
        g.height = 32;
        g.rgba.resize(size_t(g.width * g.height * 4));
        for (int y = 0; y < g.height; ++y)
            for (int x = 0; x < g.width; ++x) {
                uint8_t* p = &g.rgba[size_t((y * g.width + x) * 4)];
                p[0] = p[1] = uint8_t(x);
                p[2] = uint8_t(x * 0.8);
                p[3] = 255;
            }
        const auto m = JPWhiteBalance::automaticMapped(g, 8, why);
        assert(m && m->mapped() && m->maps[2].size() == 8 && !m->neutral());
        const JPWhiteBalance table(*m);
        for (int x : { 80, 128, 160 }) {
            const int red = table.output(0, x), blue = table.output(2, int(x * 0.8));
            assert(std::abs(red - blue) <= 12);
        }
    }

    std::puts("test_white_balance: ok");
    return 0;
}
