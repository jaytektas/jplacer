// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementsViewerCanvas.h"

#include "model/JPSystemUnits.h"

#include "model/JPPanelLocation.h"
#include "model/JPSides.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// A wheel click zooms by this much: four to double, as OpenPnP's.
const double kZoomPerTick = std::pow(2.0, 0.25);
// A major division of the scales and the reticle is about this many lines
// of text apart (OpenPnP's three-quarters of an inch), and the gap round
// the drawing a sixth of that (its eighth of an inch).
constexpr float kMajorDivisionLines = 6.0f;
constexpr float kEdgeGapShare = 1.0f / 6.0f;
// A placement's square and a fiducial's disc are a millimetre across.
constexpr double kMarkMm = 1.0;
constexpr int    kDiscSides = 24;
// How far apart the stripes over a board that is not enabled are, in edge gaps.
constexpr float kStripeGaps = 0.5f;
// The striping's and the reticle's share of their colour.
constexpr float kStripeAlpha = 0.5f;
// Long and short dashes (the array generator's), in edge gaps.
constexpr float kArrayRootDash = 0.75f, kArrayMemberDash = 0.25f;
// A reticle tick's half-length, as a share of a major division.
constexpr float kTickShare = 0.025f;
// Curves in an outline are drawn as this many straight pieces.
constexpr int kCurveSteps = 12;

JColor colour(const uint8_t* c, float alpha = 1.0f) {
    return rgba(c[0], c[1], c[2], uint8_t(float(c[3]) * alpha));
}

bool inside(const std::vector<JPPlacementsViewerCanvas::Vec>& poly, double x, double y) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const auto& a = poly[i];
        const auto& b = poly[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

std::string fixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

} // namespace

void JPPlacementsViewerCanvas::Rect::add(const Vec& p) {
    const double x1 = std::max(x + w, p.x), y1 = std::max(y + h, p.y);
    x = std::min(x, p.x);
    y = std::min(y, p.y);
    w = x1 - x;
    h = y1 - y;
}

void JPPlacementsViewerCanvas::Rect::add(const Rect& r) {
    add(Vec { r.x, r.y });
    add(Vec { r.x + r.w, r.y + r.h });
}

JPPlacementsViewerCanvas::JPPlacementsViewerCanvas(JSceneGraph& graph) : JControl(graph, "JPPlacementsViewerCanvas") {
    setHSizePolicy(JSizePolicyMode::Expanding, 1);
    setVSizePolicy(JSizePolicyMode::Expanding, 1);
    setContextMenu(nullptr);
}

void JPPlacementsViewerCanvas::setRoot(JPPlacementsHolderLocation* root, bool isJob) {
    m_root = root;
    m_isJob = isJob;
    regenerate();
}

bool JPPlacementsViewerCanvas::atRootOrJobTop(const JPPlacementsHolderLocation* l) const {
    const bool jobTopChild = m_isJob && l->parent == m_root;
    return (l == m_root || jobTopChild) && l->kind() == JPPlacementsHolderLocation::Kind::Panel;
}

std::vector<const JPPlacement*> JPPlacementsViewerCanvas::placementsOf(JPPlacementsHolderLocation* l) {
    std::vector<const JPPlacement*> out;
    if (!l->holder) return out;
    for (const JPPlacement& p : l->holder->placements) out.push_back(&p);
    if (atRootOrJobTop(l))
        for (JPPlacement& p : static_cast<JPPanel*>(l->holder.get())->pseudoPlacements()) {
            m_pseudo.push_back(std::move(p));
            out.push_back(&m_pseudo.back());
        }
    return out;
}

void JPPlacementsViewerCanvas::regenerate() {
    const std::optional<Rect> old = m_graphics;
    m_outlines.clear();
    m_marks.clear();
    m_pseudo.clear();
    // The job's bounds start at its origin; a board's or panel's at its outline.
    m_graphics = m_isJob ? std::optional<Rect>(Rect {}) : std::nullopt;
    if (m_root && m_root->holder) generate(m_root);
    const bool changed = !old || !m_graphics || old->x != m_graphics->x || old->y != m_graphics->y ||
                         old->w != m_graphics->w || old->h != m_graphics->h;
    if (changed) fitView();
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

void JPPlacementsViewerCanvas::generate(JPPlacementsHolderLocation* l) {
    if (!l || !l->holder) return;
    const bool atRoot = l == m_root;
    const bool selected = viewing != Viewing::Selected ||
                          std::find(m_selections.begin(), m_selections.end(), l) != m_selections.end();
    if ((!atRoot || !m_isJob) && selected) {
        // The outline, in the root's coordinates.
        const JPProfile profile = l->holder->outline();
        const double toMm = JPLength(1, profile.units).convertToUnits(JPLengthUnit::Millimeters).value();
        const JPAffineTransform t = l->localToGlobalTransform();
        Outline o { l, {}, {} };
        Polygon cur;
        Vec last { 0, 0 };
        auto put = [&](double x, double y) {
            Vec p;
            t.apply(x * toMm, y * toMm, p.x, p.y);
            cur.push_back(p);
            last = { x, y };
        };
        size_t k = 0;
        for (const int seg : profile.segmentTypes) {
            const auto& pts = profile.segmentPoints;
            switch (seg) {
                case JPProfile::MoveTo:
                    if (cur.size() > 1) o.polygons.push_back(cur);
                    cur.clear();
                    if (k + 1 < pts.size()) put(pts[k], pts[k + 1]);
                    k += 2;
                    break;
                case JPProfile::LineTo:
                    if (k + 1 < pts.size()) put(pts[k], pts[k + 1]);
                    k += 2;
                    break;
                case JPProfile::QuadTo:
                    if (k + 3 < pts.size()) {
                        const Vec a = last;
                        for (int i = 1; i <= kCurveSteps; ++i) {
                            const double s = double(i) / kCurveSteps, r = 1 - s;
                            put(r * r * a.x + 2 * r * s * pts[k] + s * s * pts[k + 2],
                                r * r * a.y + 2 * r * s * pts[k + 1] + s * s * pts[k + 3]);
                        }
                    }
                    k += 4;
                    break;
                case JPProfile::CubicTo:
                    if (k + 5 < pts.size()) {
                        const Vec a = last;
                        for (int i = 1; i <= kCurveSteps; ++i) {
                            const double s = double(i) / kCurveSteps, r = 1 - s;
                            put(r * r * r * a.x + 3 * r * r * s * pts[k] + 3 * r * s * s * pts[k + 2] + s * s * s * pts[k + 4],
                                r * r * r * a.y + 3 * r * r * s * pts[k + 1] + 3 * r * s * s * pts[k + 3] + s * s * s * pts[k + 5]);
                        }
                    }
                    k += 6;
                    break;
                case JPProfile::Close:
                    if (cur.size() > 1) o.polygons.push_back(cur);
                    cur.clear();
                    break;
            }
        }
        if (cur.size() > 1) o.polygons.push_back(cur);
        bool first = true;
        for (const Polygon& p : o.polygons)
            for (const Vec& v : p) {
                if (first) o.bounds = Rect { v.x, v.y, 0, 0 };
                else o.bounds.add(v);
                first = false;
            }
        if (!m_graphics) m_graphics = o.bounds;
        else m_graphics->add(o.bounds);

        // Its placements and fiducials, each a millimetre square or disc.
        for (const JPPlacement* p : placementsOf(l)) {
            const JPLocation loc = l->placementLocation(p->location).convertToUnits(JPLengthUnit::Millimeters);
            m_graphics->add(Vec { loc.x(), loc.y() });
            const double a = ((atRoot && l->globalSide() != p->side) ? -1 : 1) * loc.rotation() * M_PI / 180;
            const double d = kMarkMm * 0.5, c = std::cos(a), s = std::sin(a);
            Mark m { l, p, {} };
            if (p->type == JPPlacement::Type::Placement) {
                for (const Vec& v : { Vec { -d, -d }, Vec { d, -d }, Vec { d, d }, Vec { -d, d } })
                    m.area.push_back({ loc.x() + v.x * c - v.y * s, loc.y() + v.x * s + v.y * c });
            } else {
                for (int i = 0; i < kDiscSides; ++i) {
                    const double t2 = 2 * M_PI * i / kDiscSides;
                    m.area.push_back({ loc.x() + d * std::cos(t2), loc.y() + d * std::sin(t2) });
                }
            }
            m_marks.push_back(std::move(m));
        }
        m_outlines.push_back(std::move(o));
    }
    if ((atRoot || viewing != Viewing::Children) && l->kind() == JPPlacementsHolderLocation::Kind::Panel)
        for (JPPlacementsHolderLocation* child : static_cast<JPPanelLocation*>(l)->children()) {
            if (viewing == Viewing::Selected && child->kind() != JPPlacementsHolderLocation::Kind::Panel &&
                std::find(m_selections.begin(), m_selections.end(), child) == m_selections.end())
                continue;
            generate(child);
        }
}

const JPPlacementsViewerCanvas::Mark* JPPlacementsViewerCanvas::markOf(const JPPlacement* p) const {
    for (const Mark& m : m_marks)
        if (m.placement == p) return &m;
    return nullptr;
}

// ---- the view ---------------------------------------------------------------

JRect JPPlacementsViewerCanvas::drawingArea() const {
    const JRect b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    const JStyle& st = JStyle::current();
    const float top = st.gridHeaderHeight;
    const float left = JTextHelper::measureWidth("-0000.0") + 3 * st.spacing;
    return { b.x + left, b.y + top, std::max(1.f, b.width - left), std::max(1.f, b.height - top) };
}

float JPPlacementsViewerCanvas::edgeGap() const {
    return JTextHelper::lineHeight() * kMajorDivisionLines * kEdgeGapShare;
}

void JPPlacementsViewerCanvas::fitView() {
    const JRect a = drawingArea();
    const double gap = edgeGap();
    const double vpW = std::max(1.0, a.width - 2 * gap), vpH = std::max(1.0, a.height - 2 * gap);
    const double aspect = vpW / vpH;
    if (m_graphics) {
        const Rect& g = *m_graphics;
        m_default = { g.x, g.y, std::max(g.w, aspect * g.h), std::max(g.h, g.w / aspect) };
        if (m_default.w <= 0 || m_default.h <= 0) m_default = { g.x, g.y, aspect, 1 };
    } else {
        m_default = { 0, 0, aspect, 1 };
    }
    m_viewable = m_default;
    m_zoom = 1.0;
    m_lastW = a.width;
    m_lastH = a.height;
}

void JPPlacementsViewerCanvas::resized() {
    // As OpenPnP: the same corner, the zoom kept, the new shape.
    const Rect keep = m_viewable;
    const double zoom = m_zoom;
    fitView();
    m_zoom = zoom;
    m_viewable = { keep.x, keep.y, m_default.w / zoom, m_default.h / zoom };
}

double JPPlacementsViewerCanvas::scale() const {
    const JRect a = drawingArea();
    return std::max(1e-9, (a.width - 2 * edgeGap()) / m_viewable.w);
}

JPPlacementsViewerCanvas::Vec JPPlacementsViewerCanvas::toScreen(const Vec& p) const {
    const JRect a = drawingArea();
    const double s = scale(), gap = edgeGap();
    const double vx = viewFromTop ? s * (p.x - m_viewable.x) : s * (m_viewable.x + m_viewable.w - p.x);
    const double vy = s * (p.y - m_viewable.y);
    return { a.x + gap + vx, a.y + a.height - gap - vy };
}

JPPlacementsViewerCanvas::Vec JPPlacementsViewerCanvas::toObject(float sx, float sy) const {
    const JRect a = drawingArea();
    const double s = scale(), gap = edgeGap();
    const double vx = sx - a.x - gap, vy = a.y + a.height - gap - sy;
    const double x = viewFromTop ? vx / s + m_viewable.x : m_viewable.x + m_viewable.w - vx / s;
    return { x, vy / s + m_viewable.y };
}

// ---- drawing ----------------------------------------------------------------

void JPPlacementsViewerCanvas::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    const JRect a = drawingArea();
    if (a.width != m_lastW || a.height != m_lastH) resized();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::Surface1);
    buf.pushRectangle(a.x, a.y, a.width, a.height, Colors::ChartBg);
    buf.pushClip(a.x, a.y, a.width, a.height);
    drawOutlines(buf);
    const double gap = edgeGap();
    if (showFiducials) drawPlacementMarks(buf, JPPlacement::Type::Fiducial);
    if (showPlacements) drawPlacementMarks(buf, JPPlacement::Type::Placement);
    if (showLocations)
        for (const Outline& o : m_outlines) {
            // The corner its location is, on the side seen.
            JPLocation offset(JPLengthUnit::Millimeters);
            if ((viewFromTop && o.where->globalSide() == JPSide::Bottom) ||
                (!viewFromTop && o.where->globalSide() == JPSide::Top))
                offset = offset.derive(o.where->holder->dimensions.lengthX().convertToUnits(JPLengthUnit::Millimeters).value(),
                                       std::nullopt, std::nullopt, std::nullopt);
            drawLocationMark(buf, gap, o.where->placementLocation(offset));
        }
    if (showOrigins)
        for (const Outline& o : m_outlines) drawOriginMark(buf, *o.where);
    if (m_isJob && m_root) drawOriginMark(buf, *m_root);
    drawReticleAndScales(buf);
    buf.popClip();
}

void JPPlacementsViewerCanvas::drawOutlines(JPrimitiveBuffer& buf) {
    const JStyle& st = JStyle::current();
    const float wide = 2 * st.borderWidth;
    const double gap = edgeGap();
    JVectorCanvas vg;
    for (const Outline& o : m_outlines) {
        // Its placements and fiducials on the side seen.
        if (showPlacements || showFiducials) {
            const JPSide seen = JPSides::flip(o.where->globalSide(), !viewFromTop);
            for (const Mark& m : m_marks) {
                if (m.where != o.where || m.placement->side != seen) continue;
                std::vector<JVectorCanvas::JVec2> pts;
                for (const Vec& v : m.area) {
                    const Vec s = toScreen(v);
                    pts.push_back({ float(s.x), float(s.y) });
                }
                if (showPlacements && m.placement->type == JPPlacement::Type::Placement)
                    vg.strokePolyline(pts, wide, JPaint::solid(colour(m.placement->enabled ? Colors::TextPrimary : Colors::MutedText)), true);
                else if (showFiducials && m.placement->type == JPPlacement::Type::Fiducial)
                    vg.fillConvex(pts, JPaint::solid(colour(m.placement->enabled ? Colors::Warning : Colors::MutedText)));
            }
        }
        const bool topSeen = (o.where->globalSide() == JPSide::Top) == viewFromTop;
        const JPaint ink = JPaint::solid(colour(topSeen ? Colors::Success : Colors::Accent));
        const double dash = o.where == arrayRoot ? kArrayRootDash * gap
                          : std::find(arrayMembers.begin(), arrayMembers.end(), o.where) != arrayMembers.end()
                              ? kArrayMemberDash * gap
                              : 0;
        for (const Polygon& poly : o.polygons) {
            std::vector<JVectorCanvas::JVec2> pts;
            for (const Vec& v : poly) {
                const Vec s = toScreen(v);
                pts.push_back({ float(s.x), float(s.y) });
            }
            if (!o.where->locallyEnabled) {
                // Struck through: stripes across it, where they fall inside.
                std::vector<Vec> sp;
                for (const auto& p : pts) sp.push_back({ p.x, p.y });
                double minS = 1e300, maxS = -1e300;
                for (const Vec& p : sp) {
                    minS = std::min(minS, p.x + p.y);
                    maxS = std::max(maxS, p.x + p.y);
                }
                const double step = kStripeGaps * gap;
                for (double c = std::floor(minS / step) * step; c <= maxS; c += step) {
                    // The line x + y = c against the outline's edges.
                    std::vector<double> xs;
                    for (size_t i = 0, j = sp.size() - 1; i < sp.size(); j = i++) {
                        const double fi = sp[i].x + sp[i].y - c, fj = sp[j].x + sp[j].y - c;
                        if ((fi > 0) != (fj > 0)) xs.push_back(sp[i].x + (sp[j].x - sp[i].x) * fi / (fi - fj));
                    }
                    std::sort(xs.begin(), xs.end());
                    for (size_t i = 0; i + 1 < xs.size(); i += 2)
                        vg.drawLine(float(xs[i]), float(c - xs[i]), float(xs[i + 1]), float(c - xs[i + 1]), wide,
                                    JPaint::solid(colour(Colors::Danger, kStripeAlpha)));
                }
            }
            if (dash <= 0) {
                vg.strokePolyline(pts, wide, ink, true);
                continue;
            }
            // Dashed: on and off along the closed outline.
            bool on = true;
            double left = dash;
            for (size_t i = 0; i < pts.size(); ++i) {
                JVectorCanvas::JVec2 p = pts[i];
                const JVectorCanvas::JVec2 q = pts[(i + 1) % pts.size()];
                double len = std::hypot(q.x - p.x, q.y - p.y);
                while (len > 0) {
                    const double take = std::min(left, len);
                    const float t = float(take / len);
                    const JVectorCanvas::JVec2 r { p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t };
                    if (on) vg.drawLine(p.x, p.y, r.x, r.y, wide, ink);
                    p = r;
                    len -= take;
                    left -= take;
                    if (left <= 0) {
                        on = !on;
                        left = dash;
                    }
                }
            }
        }
    }
    vg.flush(buf);
}

void JPPlacementsViewerCanvas::drawLocationMark(JPrimitiveBuffer& buf, double sizePx, const JPLocation& at) {
    // A plus: its +Y leg in the accent, the rest in red, as OpenPnP's camera crosshair.
    const JPLocation l = at.convertToUnits(JPLengthUnit::Millimeters);
    const double size = sizePx / scale(), r = l.rotation() * M_PI / 180, c = std::cos(r), s = std::sin(r);
    auto pt = [&](double x, double y) { return toScreen({ l.x() + x * c - y * s, l.y() + x * s + y * c }); };
    const float w = JStyle::current().borderWidth;
    JVectorCanvas vg;
    auto line = [&](Vec a, Vec b, const uint8_t* ink) {
        vg.drawLine(float(a.x), float(a.y), float(b.x), float(b.y), w, JPaint::solid(colour(ink)));
    };
    line(pt(0, 0), pt(0, size), Colors::Accent);
    line(pt(-size, 0), pt(size, 0), Colors::Danger);
    line(pt(0, 0), pt(0, -size), Colors::Danger);
    vg.flush(buf);
}

void JPPlacementsViewerCanvas::drawOriginMark(JPrimitiveBuffer& buf, const JPPlacementsHolderLocation& phl) {
    // Arrows along its +X (red) and +Y (the accent), mirrored for a bottom side up.
    const JPLocation l = phl.placementLocation(JPLocation(JPLengthUnit::Millimeters)).convertToUnits(JPLengthUnit::Millimeters);
    const double r = l.rotation() * M_PI / 180, c = std::cos(r), s = std::sin(r);
    const double mirror = phl.globalSide() == JPSide::Top ? 1 : -1;
    const double d = edgeGap() / (0.75 * scale());
    auto pt = [&](double x, double y) {
        x *= mirror;
        return toScreen({ l.x() + x * c - y * s, l.y() + x * s + y * c });
    };
    const float w = JStyle::current().borderWidth;
    JVectorCanvas vg;
    auto line = [&](double x0, double y0, double x1, double y1, const uint8_t* ink) {
        const Vec a = pt(x0, y0), b = pt(x1, y1);
        vg.drawLine(float(a.x), float(a.y), float(b.x), float(b.y), w, JPaint::solid(colour(ink)));
    };
    line(-d / 10, -d / 10, -d / 10, d, Colors::Accent);
    line(d / 10, d / 10, d / 10, d, Colors::Accent);
    line(-2 * d / 5, d - d / 5, 0, d + d / 5, Colors::Accent);
    line(2 * d / 5, d - d / 5, 0, d + d / 5, Colors::Accent);
    line(-d / 10, -d / 10, d, -d / 10, Colors::Danger);
    line(d / 10, d / 10, d, d / 10, Colors::Danger);
    line(d - d / 5, 2 * d / 5, d + d / 5, 0, Colors::Danger);
    line(d - d / 5, -2 * d / 5, d + d / 5, 0, Colors::Danger);
    vg.flush(buf);
}

void JPPlacementsViewerCanvas::drawPlacementMarks(JPrimitiveBuffer& buf, JPPlacement::Type type) {
    const double size = edgeGap() / 2.0;
    for (const Mark& m : m_marks) {
        if (m.placement->type != type) continue;
        const bool seen = viewFromTop ? m.placement->side == m.where->globalSide() : m.placement->side != m.where->globalSide();
        if (!seen) continue;
        const JPLocation local = m.placement->location.multiply(1, 1, 1, viewFromTop ? 1 : -1);
        drawLocationMark(buf, size, m.where->placementLocation(local));
    }
}

void JPPlacementsViewerCanvas::drawReticleAndScales(JPrimitiveBuffer& buf) {
    const JStyle& st = JStyle::current();
    const JRect b = m_graph.getLayoutConst(m_nodeId).boundingBox;
    const JRect a = drawingArea();
    const double major = JTextHelper::lineHeight() * kMajorDivisionLines;
    // The division: 1, 2 or 5 times a power of ten, about a major division apart.
    const Vec lo = toObject(a.x, a.y + a.height), hi = toObject(a.x + a.width, a.y);
    const double minX = std::min(lo.x, hi.x), maxX = std::max(lo.x, hi.x), minY = lo.y, maxY = hi.y;
    // Worked out in the System Units (a round number of them a division), drawn in millimetres.
    const double scale = JPSystemUnits::shown(1.0);   // units a millimetre
    double perUnits = scale * (maxX - minX) / (a.width / major);
    const double power = std::floor(std::log10(perUnits));
    double mult = perUnits / std::pow(10, power);
    mult = mult >= 2.5 ? 5 : mult >= 1.5 ? 2 : 1;
    perUnits = mult * std::pow(10, power);
    const double perDivision = perUnits / scale;
    const double perTick = mult == 5 ? perDivision / 5 : perDivision / 10;
    const bool inches = JPSystemUnits::inches();
    std::string unit = inches ? "in" : "mm";
    double shown = scale;   // a millimetre, as labelled
    int decimals = 0;
    if (inches ? perUnits >= 1 : perUnits > 1) decimals = 0;
    else if (inches ? perUnits >= 0.1 : perUnits > 0.1) decimals = 1;
    else {
        unit = inches ? "mil" : "um";
        shown = scale * 1000;
    }
    const double x0 = perDivision * std::floor(minX / perDivision), x1 = perDivision * std::ceil(maxX / perDivision);
    const double y0 = perDivision * std::floor(minY / perDivision), y1 = perDivision * std::ceil(maxY / perDivision);
    JVectorCanvas vg;
    const float w = st.borderWidth;
    if (showReticle) {
        const JPaint ink = JPaint::solid(colour(Colors::ChartCrosshair));
        const double tick = kTickShare * major;
        for (double x = x0; x <= x1 + 1e-9; x += perDivision) {
            const Vec s0 = toScreen({ x, y0 }), s1 = toScreen({ x, y1 });
            vg.drawLine(float(s0.x), float(s0.y), float(s1.x), float(s1.y), w, ink);
            for (double y = y0; y <= y1 + 1e-9; y += perTick) {
                const Vec m = toScreen({ x, y });
                vg.drawLine(float(m.x - tick), float(m.y), float(m.x + tick), float(m.y), w, ink);
            }
        }
        for (double y = y0; y <= y1 + 1e-9; y += perDivision) {
            const Vec s0 = toScreen({ x0, y }), s1 = toScreen({ x1, y });
            vg.drawLine(float(s0.x), float(s0.y), float(s1.x), float(s1.y), w, ink);
            for (double x = x0; x <= x1 + 1e-9; x += perTick) {
                const Vec m = toScreen({ x, y });
                vg.drawLine(float(m.x), float(m.y - tick), float(m.x), float(m.y + tick), w, ink);
            }
        }
    }
    vg.flush(buf);
    buf.popClip();

    // The scales: along the top for X, down the left for Y, and the unit in the corner.
    const float lh = JTextHelper::lineHeight();
    const float tickLen = st.spacing;
    buf.pushClip(a.x, b.y, a.width, a.y - b.y);
    for (double x = x0; x <= x1 + 1e-9; x += perDivision) {
        const Vec s = toScreen({ x, y1 });
        buf.pushRectangle(float(s.x), a.y - tickLen, w, tickLen, Colors::LabelText);
        const std::string t = fixed(x * shown, decimals);
        JTextHelper::pushText(buf, float(s.x) - JTextHelper::measureWidth(t) * 0.5f, a.y - tickLen - lh, t, Colors::LabelText);
    }
    buf.popClip();
    buf.pushClip(b.x, a.y, a.x - b.x, a.height);
    for (double y = y0; y <= y1 + 1e-9; y += perDivision) {
        const Vec s = toScreen({ x0, y });
        buf.pushRectangle(a.x - tickLen, float(s.y), tickLen, w, Colors::LabelText);
        const std::string t = fixed(y * shown, decimals);
        JTextHelper::pushText(buf, a.x - tickLen - st.spacing * 0.5f - JTextHelper::measureWidth(t), float(s.y) - lh * 0.5f, t,
                              Colors::LabelText);
    }
    buf.popClip();
    JTextHelper::pushText(buf, b.x + (a.x - b.x - JTextHelper::measureWidth(unit)) * 0.5f, b.y + (a.y - b.y - lh) * 0.5f,
                          unit, Colors::LabelText);
    buf.pushClip(a.x, a.y, a.width, a.height);   // as the caller left it
}

// ---- the mouse --------------------------------------------------------------

void JPPlacementsViewerCanvas::handleMousePress(float mx, float my) {
    m_pressed = true;
    m_dragX = mx;
    m_dragY = my;
}

void JPPlacementsViewerCanvas::handleMouseMove(float mx, float my) {
    if (!m_pressed) return;
    // Panned: what was under the pointer stays under it.
    const Vec from = toObject(m_dragX, m_dragY), to = toObject(mx, my);
    m_viewable.x += from.x - to.x;
    m_viewable.y += from.y - to.y;
    m_dragX = mx;
    m_dragY = my;
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

void JPPlacementsViewerCanvas::handleMouseRelease(float, float) { m_pressed = false; }

bool JPPlacementsViewerCanvas::handleScroll(float mx, float my, float wheel) {
    if (!isPointInside(mx, my)) return false;
    const double zoom = 1e-6 * std::round(std::max(1.0, std::pow(kZoomPerTick, double(wheel)) * m_zoom) * 1e6);
    if (zoom == m_zoom) return true;
    if (zoom > 1) {
        // About the pointer: the point under it stays there.
        const Vec p = toObject(mx, my);
        const double w = m_default.w / zoom, h = m_default.h / zoom;
        m_viewable = { p.x - w * (p.x - m_viewable.x) / m_viewable.w, p.y - h * (p.y - m_viewable.y) / m_viewable.h, w, h };
    } else {
        m_viewable = m_default;
    }
    m_zoom = zoom;
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    return true;
}

void JPPlacementsViewerCanvas::prepareContextMenu(float mx, float my) {
    setContextMenu(nullptr);
    if (!m_root) return;
    const Vec p = toObject(mx, my);
    // The deepest board or panel under the pointer (the longest unique id).
    JPPlacementsHolderLocation* where = nullptr;
    std::string uniqueId;
    bool any = false;
    for (const Outline& o : m_outlines)
        for (const Polygon& poly : o.polygons)
            if (inside(poly, p.x, p.y)) {
                const std::string id = o.where->uniqueId();
                if (!any || id.size() >= uniqueId.size()) {
                    uniqueId = id;
                    where = o.where;
                    any = true;
                }
            }
    // A placement or fiducial shown under it, on the side seen.
    const JPPlacement* placement = nullptr;
    if (showPlacements || showFiducials) {
        const JPSide seen = where ? JPSides::flip(where->globalSide(), !viewFromTop) : JPSides::flip(JPSide::Top, !viewFromTop);
        for (const Mark& m : m_marks) {
            if (where && m.where != where) continue;
            const bool shown = (showPlacements && m.placement->type == JPPlacement::Type::Placement) ||
                               (showFiducials && m.placement->type == JPPlacement::Type::Fiducial);
            if (shown && m.placement->side == seen && inside(m.area, p.x, p.y)) {
                placement = m.placement;
                break;
            }
        }
    }
    if (!placement && !where) return;
    // What may be turned on or off here, as OpenPnP allows it.
    const bool directChild = where && m_root->kind() == JPPlacementsHolderLocation::Kind::Panel &&
                             where->parent == m_root && !placement;
    const bool enablable = m_isJob || (uniqueId.empty() && placement) || directChild || (!where && placement);

    m_menu = std::make_unique<JMenu>("Viewer");
    JSceneGraph& g = m_graph;
    if (placement) {
        const Mark* m = markOf(placement);
        JPPlacementsHolderLocation* at = m ? m->where : where;
        m_menu->add(g, placement->type == JPPlacement::Type::Placement ? "Placement" : "Fiducial")->setEnabled(false);
        m_menu->add(g, "Id:   " + (uniqueId.empty() ? std::string() : uniqueId + JPPlacementsHolderLocation::kIdDelimiter) +
                           placement->id)->setEnabled(false);
        m_menu->add(g, "Part: " + (placement->partId.empty() ? std::string("unassigned") : placement->partId))->setEnabled(false);
        m_menu->add(g, std::string("Side: ") + JPSides::name(at ? at->globalSide() : JPSide::Top))->setEnabled(false);
        m_menu->addSeparator(g);
        JMenuItem* enabled = m_menu->add(g, "Enabled?");
        enabled->setCheckable(true);
        enabled->setChecked(placement->enabled);
        enabled->setEnabled(enablable);
        const std::string id = placement->id;
        const bool on = !placement->enabled;
        enabled->onTriggered.connect([this, at, id, on] {
            if (onPlacementEnabled) onPlacementEnabled(at, id, on);
        });
        if (m_isJob && at) {
            m_menu->addSeparator(g);
            JMenuItem* placed = m_menu->add(g, "Placed?");
            placed->setCheckable(true);
            const bool was = placedOf && placedOf(at, id);
            placed->setChecked(was);
            placed->onTriggered.connect([this, at, id, was] {
                if (onPlacementPlaced) onPlacementPlaced(at, id, !was);
            });
            m_menu->addSeparator(g);
            const std::string type = placement->type == JPPlacement::Type::Placement ? "Placement" : "Fiducial";
            JMenuItem* centre = m_menu->add(g, "Center Camera on " + type);
            centre->setTooltip("Centers the top camera on the " + type);
            const JPLocation there = at->placementLocation(placement->location);
            centre->onTriggered.connect([this, there] {
                if (onCenterCamera) onCenterCamera(there);
            });
        }
    } else {
        m_menu->add(g, where->kind() == JPPlacementsHolderLocation::Kind::Board ? "Board" : "Panel")->setEnabled(false);
        if (!uniqueId.empty()) m_menu->add(g, "Id:   " + uniqueId)->setEnabled(false);
        m_menu->add(g, "Name: " + where->holder->name.value_or(""))->setEnabled(false);
        m_menu->add(g, std::string("Side: ") + JPSides::name(JPSides::flip(where->globalSide(), !viewFromTop)))->setEnabled(false);
        m_menu->addSeparator(g);
        JMenuItem* enabled = m_menu->add(g, "Enabled?");
        enabled->setCheckable(true);
        enabled->setChecked(where->locallyEnabled);
        enabled->setEnabled(enablable);
        const bool on = !where->locallyEnabled;
        enabled->onTriggered.connect([this, where, on] {
            if (onLocationEnabled) onLocationEnabled(where, on);
        });
        JMenuItem* fids = m_menu->add(g, "Check Fids?");
        fids->setCheckable(true);
        fids->setChecked(where->checkFiducials);
        fids->setEnabled(enablable);
        const bool check = !where->checkFiducials;
        fids->onTriggered.connect([this, where, check] {
            if (onCheckFiducials) onCheckFiducials(where, check);
        });
        if (m_isJob) {
            const std::string type = where->kind() == JPPlacementsHolderLocation::Kind::Board ? "Board Location" : "Panel Location";
            m_menu->addSeparator(g);
            JMenuItem* centre = m_menu->add(g, "Center Camera on " + type);
            centre->setTooltip("Centers the top camera on the " + type);
            // Its origin; a board or panel bottom side up, the corner at its far X (as OpenPnP's).
            JPLocation origin(JPLengthUnit::Millimeters);
            if (where->globalSide() == JPSide::Bottom && where->holder)
                origin = JPLocation(where->holder->dimensions.units(), where->holder->dimensions.x(), 0, 0, 0);
            const JPLocation there = where->placementLocation(origin);
            centre->onTriggered.connect([this, there] {
                if (onCenterCamera) onCenterCamera(there);
            });
            m_menu->addSeparator(g);
            JMenuItem* check = m_menu->add(g, "Run Fiduicial Check on " + type);
            check->setTooltip("Runs a fiducial check and adjusts the position and rotation of the " + type);
            check->onTriggered.connect([this, where] {
                if (onFiducialCheck) onFiducialCheck(where);
            });
        }
    }
    setContextMenu(m_menu.get());
}

} // inline namespace jf
