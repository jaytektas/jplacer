// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlotView.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/graphics/Chart.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// A map's cells across its longer side.
constexpr int kMapCells = 48;
// Each cell is coloured by this many of the spots nearest it, weighted by
// how near.
constexpr size_t kNearest = 4;

JColor colour(const uint8_t c[4]) { return rgba(c[0], c[1], c[2], c[3]); }

JColor toneColour(JPPlot::Tone t) {
    switch (t) {
        case JPPlot::Tone::First:  return colour(Colors::Danger);
        case JPPlot::Tone::Second: return colour(Colors::Success);
        case JPPlot::Tone::Third:  return colour(Colors::Accent);
        case JPPlot::Tone::Muted:  return colour(Colors::MutedText);
    }
    return colour(Colors::Accent);
}

JColor mix(JColor a, JColor b, double t) {
    auto m = [t](uint8_t x, uint8_t y) { return uint8_t(std::lround(x + (y - x) * t)); };
    return rgba(m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a));
}

// Cool to hot: the accent, the warning colour, the danger colour.
JColor heat(double t) {
    t = std::clamp(t, 0.0, 1.0);
    const JColor cool = colour(Colors::Accent), warm = colour(Colors::Warning), hot = colour(Colors::Danger);
    return t < 0.5 ? mix(cool, warm, t * 2) : mix(warm, hot, t * 2 - 1);
}

} // namespace

JPPlotView::JPPlotView(JSceneGraph& graph, std::shared_ptr<const JPPlot> plot)
    : JWidget(graph, "JPPlotView"), m_plot(std::move(plot)) {}

void JPPlotView::setPlot(std::shared_ptr<const JPPlot> plot) {
    m_plot = std::move(plot);
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

void JPPlotView::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    if (!m_plot) return;
    const JRect b = bounds();
    buf.pushClip(b.x, b.y, b.width, b.height);
    if (m_plot->kind == JPPlot::Kind::Map) drawMap(buf, b);
    else drawChart(buf, b);
    buf.popClip();
}

void JPPlotView::drawChart(JPrimitiveBuffer& buf, const JRect& b) const {
    const JStyle& st = JStyle::current();
    JChart chart;
    chart.setRect(b.x, b.y, b.width, b.height);
    chart.setBackground(colour(Colors::ChartBg));
    chart.setAxisTitles(m_plot->xTitle, m_plot->yTitle, m_plot->y2Title);
    if (m_plot->xLo < m_plot->xHi) chart.setXRange(m_plot->xLo, m_plot->xHi);
    if (m_plot->yLo < m_plot->yHi) chart.setYRange(m_plot->yLo, m_plot->yHi);
    if (m_plot->y2Lo < m_plot->y2Hi) chart.setY2Range(m_plot->y2Lo, m_plot->y2Hi);
    chart.setShowLegend(m_plot->series.size() > 1);
    const bool scatter = m_plot->kind == JPPlot::Kind::Scatter;
    const bool dots = scatter || m_plot->kind == JPPlot::Kind::Points;
    chart.setLogX(m_plot->logX);
    for (const JPPlot::Series& s : m_plot->series) {
        const int i = chart.addSeries(s.label, toneColour(s.tone), st.borderWidth);
        if (dots) chart.series(i).type = JSeriesType::Scatter;
        chart.series(i).secondary = s.secondary;
        for (const JPPlot::Point& p : s.points) chart.addPoint(i, p.x, p.y);
    }
    if (scatter) {
        // The same scale both ways, about the origin, the circle inside.
        double reach = m_plot->circle;
        for (const JPPlot::Series& s : m_plot->series)
            for (const JPPlot::Point& p : s.points) reach = std::max({ reach, std::abs(p.x), std::abs(p.y) });
        reach = reach > 0 ? reach * 1.1 : 1;
        chart.setXRange(-reach, reach);
        chart.setYRange(-reach, reach);
        // Laid out once to learn the plotting area, then the longer side's
        // range widened so a pixel is as much either way (a circle is round).
        JPrimitiveBuffer scratch;
        chart.render(scratch);
        float ax, ay, aw, ah;
        chart.plotArea(ax, ay, aw, ah);
        if (aw > 0 && ah > 0) {
            if (aw > ah) chart.setXRange(-reach * aw / ah, reach * aw / ah);
            else chart.setYRange(-reach * ah / aw, reach * ah / aw);
        }
        if (m_plot->circle > 0) {
            const int c = chart.addSeries("outlier limit", toneColour(JPPlot::Tone::Second), st.borderWidth);
            constexpr int kPieces = 72;
            for (int i = 0; i <= kPieces; ++i) {
                const double a = 2 * M_PI * i / kPieces;
                chart.addPoint(c, m_plot->circle * std::cos(a), m_plot->circle * std::sin(a));
            }
        }
    }
    chart.render(buf);
}

void JPPlotView::drawMap(JPrimitiveBuffer& buf, const JRect& b) const {
    const JStyle& st = JStyle::current();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::ChartBg, 0.f);
    if (m_plot->spots.empty() || m_plot->width <= 0 || m_plot->height <= 0) return;
    // The area kept to its shape, with a line under it for the scale.
    const float lh = JTextHelper::lineHeight(), pad = st.spacing;
    const float aw = b.width - 2 * pad, ah = b.height - 2 * pad - lh;
    if (aw <= 0 || ah <= 0) return;
    const float scale = std::min(aw / float(m_plot->width), ah / float(m_plot->height));
    const float w = float(m_plot->width) * scale, h = float(m_plot->height) * scale;
    const float x0 = b.x + (b.width - w) / 2, y0 = b.y + pad;
    double most = 0;
    for (const JPPlot::Spot& s : m_plot->spots) most = std::max(most, s.value);
    const bool wide = m_plot->width >= m_plot->height;
    const int cols = wide ? kMapCells : std::max(1, int(std::lround(kMapCells * m_plot->width / m_plot->height)));
    const int rows = wide ? std::max(1, int(std::lround(kMapCells * m_plot->height / m_plot->width))) : kMapCells;
    const float cw = w / float(cols), ch = h / float(rows);
    std::vector<std::pair<double, double>> near;   // (distance², value)
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const double px = (c + 0.5) * m_plot->width / cols, py = (r + 0.5) * m_plot->height / rows;
            near.clear();
            for (const JPPlot::Spot& s : m_plot->spots) near.push_back({ (s.x - px) * (s.x - px) + (s.y - py) * (s.y - py), s.value });
            const size_t k = std::min(kNearest, near.size());
            std::partial_sort(near.begin(), near.begin() + long(k), near.end());
            double sum = 0, weights = 0;
            for (size_t i = 0; i < k; ++i) {
                const double wgt = 1.0 / std::max(near[i].first, 1e-9);
                sum += wgt * near[i].second;
                weights += wgt;
            }
            const JColor col = heat(most > 0 ? sum / weights / most : 0);
            const uint8_t rgbaCol[4] = { col.r, col.g, col.b, col.a };
            buf.pushRectangle(x0 + c * cw, y0 + r * ch, cw + 0.5f, ch + 0.5f, rgbaCol, 0.f);
        }
    char text[96];
    std::snprintf(text, sizeof text, "0 \xE2\x80\x93 %.3g", most);
    std::string scaleText = text;
    if (!m_plot->yTitle.empty()) scaleText += " " + m_plot->yTitle;
    JTextHelper::pushText(buf, x0, y0 + h + pad / 2, scaleText, Colors::ChartAxisText, w);
}

} // inline namespace jf
