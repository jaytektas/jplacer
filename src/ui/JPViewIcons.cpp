// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPViewIcons.h"

#include "JPIcons.h"

#include <cmath>

inline namespace jf {

namespace {

// A line's width, as a share of the icon's size (as JPIcons draws).
constexpr float kLine = 0.08f;
constexpr float kPi   = 3.14159265f;

using V = JVectorCanvas::JVec2;

// Three lines with a dot before each: rows of a table.
void list(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float line = s * kLine;
    for (int i = -1; i <= 1; ++i) {
        const float y = cy + float(i) * s * 0.24f;
        vg.fillCircle(cx - s * 0.3f, y, line * 0.9f, JPaint::solid(ink));
        vg.drawLine(cx - s * 0.16f, y, cx + s * 0.34f, y, line, JPaint::solid(ink));
    }
}

// A grid of three columns: every field.
void columns(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float line = s * kLine, h = s * 0.64f, w = s * 0.72f;
    vg.strokeRect(cx - w * 0.5f, cy - h * 0.5f, w, h, line, JPaint::solid(ink));
    for (const float f : { -1.f / 6, 1.f / 6 }) vg.drawLine(cx + w * f, cy - h * 0.5f, cx + w * f, cy + h * 0.5f, line, JPaint::solid(ink));
    vg.drawLine(cx - w * 0.5f, cy - h * 0.2f, cx + w * 0.5f, cy - h * 0.2f, line, JPaint::solid(ink));
}

// A five-pointed star, its outline.
void star(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float outer = s * 0.42f, inner = outer * 0.42f;
    std::vector<V> pts;
    for (int i = 0; i < 10; ++i) {
        const float a = -kPi / 2 + float(i) * kPi / 5, r = i % 2 ? inner : outer;
        pts.push_back({ cx + r * std::cos(a), cy + r * std::sin(a) + s * 0.03f });
    }
    vg.strokePolyline(pts, s * kLine, JPaint::solid(ink), true);
}

// A lorry: its box, its cab, two wheels (a supplier's).
void truck(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float line = s * kLine;
    vg.strokeRect(cx - s * 0.42f, cy - s * 0.22f, s * 0.48f, s * 0.36f, line, JPaint::solid(ink));
    vg.strokePolyline({ { cx + s * 0.06f, cy - s * 0.08f }, { cx + s * 0.28f, cy - s * 0.08f }, { cx + s * 0.42f, cy + s * 0.04f },
                        { cx + s * 0.42f, cy + s * 0.14f }, { cx + s * 0.06f, cy + s * 0.14f } },
                      line, JPaint::solid(ink));
    for (const float x : { -0.26f, 0.26f }) vg.fillCircle(cx + s * x, cy + s * 0.22f, s * 0.08f, JPaint::solid(ink));
}

// A spanner across: production, the machine's work.
void wrench(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float line = s * 0.12f;
    vg.drawLine(cx - s * 0.3f, cy + s * 0.3f, cx + s * 0.12f, cy - s * 0.12f, line, JPaint::solid(ink));
    // Its open jaw: most of a ring, the gap away from the handle.
    vg.strokePolyline([&] {
        std::vector<V> arc;
        for (int i = 0; i <= 12; ++i) {
            const float a = kPi * 0.05f + float(i) * kPi * 1.45f / 12;
            arc.push_back({ cx + s * 0.2f + s * 0.16f * std::cos(a - kPi * 0.75f), cy - s * 0.2f + s * 0.16f * std::sin(a - kPi * 0.75f) });
        }
        return arc;
    }(), s * kLine * 1.2f, JPaint::solid(ink));
}

// A head and shoulders: someone's own.
void person(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float line = s * kLine;
    vg.strokeCircle(cx, cy - s * 0.16f, s * 0.15f, line, JPaint::solid(ink));
    std::vector<V> shoulders;
    for (int i = 0; i <= 12; ++i) {
        const float a = kPi + float(i) * kPi / 12;
        shoulders.push_back({ cx + s * 0.3f * std::cos(a), cy + s * 0.36f + s * 0.26f * std::sin(a) });
    }
    vg.strokePolyline(shoulders, line, JPaint::solid(ink));
}

// A clipboard: a board with its clip, two lines on it (a checklist).
void clipboard(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float line = s * kLine;
    vg.strokeRect(cx - s * 0.26f, cy - s * 0.3f, s * 0.52f, s * 0.66f, line, JPaint::solid(ink));
    vg.fillRect(cx - s * 0.12f, cy - s * 0.38f, s * 0.24f, s * 0.12f, JPaint::solid(ink));
    for (const float y : { -0.06f, 0.12f }) vg.drawLine(cx - s * 0.14f, cy + s * y, cx + s * 0.14f, cy + s * y, line, JPaint::solid(ink));
}

// A label tag with its hole.
void tag(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float line = s * kLine;
    vg.strokePolyline({ { cx - s * 0.38f, cy - s * 0.2f }, { cx + s * 0.14f, cy - s * 0.2f }, { cx + s * 0.38f, cy },
                        { cx + s * 0.14f, cy + s * 0.2f }, { cx - s * 0.38f, cy + s * 0.2f } },
                      line, JPaint::solid(ink), true);
    vg.fillCircle(cx + s * 0.12f, cy, s * 0.05f, JPaint::solid(ink));
}

// A flag on its pole.
void flag(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float line = s * kLine;
    vg.drawLine(cx - s * 0.26f, cy - s * 0.38f, cx - s * 0.26f, cy + s * 0.38f, line, JPaint::solid(ink));
    vg.fillConvex({ { cx - s * 0.26f, cy - s * 0.36f }, { cx + s * 0.32f, cy - s * 0.2f }, { cx - s * 0.26f, cy - s * 0.02f } },
                  JPaint::solid(ink));
}

// A box seen from above a corner: stock, a package.
void box(JVectorCanvas& vg, float cx, float cy, float s, const JColor& ink) {
    const float line = s * kLine;
    const V top { cx, cy - s * 0.36f }, left { cx - s * 0.34f, cy - s * 0.18f }, right { cx + s * 0.34f, cy - s * 0.18f },
        mid { cx, cy }, bl { cx - s * 0.34f, cy + s * 0.2f }, br { cx + s * 0.34f, cy + s * 0.2f }, bottom { cx, cy + s * 0.38f };
    vg.strokePolyline({ left, top, right, mid, left, bl, bottom, br, right }, line, JPaint::solid(ink));
    vg.drawLine(mid.x, mid.y, bottom.x, bottom.y, line, JPaint::solid(ink));
}

struct Entry {
    JPViewIcons::Icon   icon;
    JPIconButton::Glyph glyph;
};

const std::vector<Entry>& entries() {
    static const std::vector<Entry> v {
        { { "list", "List" }, list },           { { "columns", "Columns" }, columns }, { { "star", "Star" }, star },
        { { "truck", "Truck" }, truck },        { { "wrench", "Wrench" }, wrench },    { { "eye", "Eye" }, JPIcons::eye },
        { { "person", "Person" }, person },     { { "clipboard", "Clipboard" }, clipboard },
        { { "tag", "Tag" }, tag },              { { "flag", "Flag" }, flag },          { { "box", "Box" }, box },
        { { "gear", "Gear" }, JPIcons::gear },  { { "check", "Check" }, JPIcons::check },
    };
    return v;
}

} // namespace

const std::vector<JPViewIcons::Icon>& JPViewIcons::all() {
    static const std::vector<Icon> v = [] {
        std::vector<Icon> out;
        for (const Entry& e : entries()) out.push_back(e.icon);
        return out;
    }();
    return v;
}

JPIconButton::Glyph JPViewIcons::glyph(const std::string& key) {
    for (const Entry& e : entries())
        if (key == e.icon.key) return e.glyph;
    return entries().front().glyph;
}

std::string JPViewIcons::label(const std::string& key) {
    for (const Entry& e : entries())
        if (key == e.icon.key) return e.icon.label;
    return entries().front().icon.label;
}

} // inline namespace jf
