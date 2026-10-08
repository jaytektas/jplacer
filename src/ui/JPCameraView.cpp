// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraView.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/MainThreadDispatcher.h>
#include <j/graphics/RenderPrimitive.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

// OpenPnP's capture error picture (AbstractBroadcastingCamera.getCaptureErrorImage), shown when there is no
// picture: 640 x 480, dark grey (Java's Color.darkGray), a red X in its top left, from 20 to 100 each way, its
// strokes 20 wide with Java's square ends. These are the picture's own pixels, not the theme's.
constexpr float kErrorWidth = 640, kErrorHeight = 480;
constexpr float kErrorCrossFrom = 20, kErrorCrossTo = 100, kErrorCrossStroke = 20;
constexpr uint8_t kErrorGrey = 64;

// OpenPnP's red X, on a picture whose top left is at x, y drawn `scale` screen pixels to its pixel (as on its
// 640-wide error picture): over a view with no picture, and over a live camera not calibrated.
void errorCross(JVectorCanvas& vg, float x, float y, float scale) {
    const JPaint red = JPaint::solid(rgb(255, 0, 0));
    // Square ends: each stroke runs on half its width past its ends.
    const float reach = kErrorCrossStroke * 0.5f / std::sqrt(2.f);
    const float a = (kErrorCrossFrom - reach) * scale, z = (kErrorCrossTo + reach) * scale;
    vg.drawLine(x + a, y + a, x + z, y + z, kErrorCrossStroke * scale, red);
    vg.drawLine(x + a, y + z, x + z, y + a, kErrorCrossStroke * scale, red);
}
// Each notch of the wheel zooms by this much, by sensitivity (OpenPnP's).
double zoomPerNotch(JPCameraView::ZoomSensitivity s) {
    switch (s) {
        case JPCameraView::ZoomSensitivity::High: return 2.0;
        case JPCameraView::ZoomSensitivity::Low:  return std::pow(2.0, 1.0 / 4);
        default:                                   return std::sqrt(2.0);
    }
}
// A grid's lines and a ruler's marks are no closer on screen than this many
// spacings (JStyle::spacing); closer ones are left out.
constexpr float kLeastGap = 3.f;
// The pictures a second are the mean over this many pictures (as OpenPnP's).
constexpr size_t kFpsPictures = 24;
// The histogram, as OpenPnP draws it: a column for each of the 256 levels,
// this tall at most; and the light toggle, this many control heights across,
// its sun's disc and rays as shares of that.
constexpr float  kHistogramHeight = 50.f;
constexpr float  kLightControls = 1.33f;
// OpenPnP's rotation handle: its ring 80 % of the way out from the middle, the handle a circle of about
// two controls across (OpenPnP's 50 px), snapped to the nearest 45 degrees with Alt.
constexpr float  kRingShare = 0.8f, kHandleControls = 0.9f;
constexpr double kSnapDeg = 45.0;
constexpr float  kSunDisc = 0.27f, kRayFrom = 0.36f, kRayTo = 0.5f;
constexpr int    kRays = 12;
constexpr double kPi = 3.14159265358979323846;
} // namespace

JPCameraView::JPCameraView(JSceneGraph& graph, JGpuHal& hal)
    : JWidget(graph, "JPCameraView"), m_hal(hal) {
    buildMenu();
}

void JPCameraView::buildMenu() {
    JSceneGraph& g = m_graph;
    m_menu        = std::make_unique<JMenu>("Camera");
    m_spacingMenu = std::make_unique<JMenu>("Spacing");
    m_sizeMenu    = std::make_unique<JMenu>("Size");
    m_estimateZItem = m_menu->add(g, "Estimate Z Coordinate of Object");
    m_estimateZItem->onTriggered.connect([this] {
        if (onEstimateZ) onEstimateZ();
    });
    m_nozzleHereItem = m_menu->add(g, "Move Selected Nozzle to Camera");
    m_nozzleHereItem->onTriggered.connect([this] {
        if (onMoveNozzleHere) onMoveNozzleHere();
    });
    m_zoomMenu = std::make_unique<JMenu>("Zoom Sensitivity");
    for (ZoomSensitivity z : { ZoomSensitivity::High, ZoomSensitivity::Medium, ZoomSensitivity::Low }) {
        JMenuItem* item = m_zoomMenu->add(g, name(z));
        item->setCheckable(true);
        item->onTriggered.connect([this, z] {
            m_sensitivity = z;
            if (onZoomSensitivityChanged) onZoomSensitivityChanged(z);
        });
        m_zoomItems.emplace_back(item, z);
    }
    m_menu->add(g, "Zoom Sensitivity", {}, m_zoomMenu.get());
    m_qualityMenu = std::make_unique<JMenu>("Rendering Quality");
    for (RenderingQuality q : { RenderingQuality::Low, RenderingQuality::High, RenderingQuality::BestScale }) {
        static const char* const kLabels[] = { "Low Quality", "High Quality", "Highest Quality (best scale)" };
        JMenuItem* item = m_qualityMenu->add(g, kLabels[int(q)]);
        item->setCheckable(true);
        item->onTriggered.connect([this, q] {
            setRenderingQuality(q);
            if (onRenderingQualityChanged) onRenderingQualityChanged(q);
        });
        m_qualityItems.emplace_back(item, q);
    }
    m_menu->add(g, "Rendering Quality", {}, m_qualityMenu.get());
    for (JPReticle::Kind k : JPReticle::kinds()) {
        JMenuItem* item = m_menu->add(g, JPReticle::name(k));
        item->setCheckable(true);
        item->onTriggered.connect([this, k] {
            JPReticle r = m_reticle;
            r.kind = k;
            choose(r);
        });
        m_kindItems.emplace_back(item, k);
    }
    m_uncalibrated = m_menu->add(g, "Calibrate the camera to draw to scale");
    m_uncalibrated->setEnabled(false);
    m_menu->addSeparator(g);
    auto mm = [](double v) {
        char text[32];
        std::snprintf(text, sizeof text, "%g mm", v);
        return std::string(text);
    };
    for (double v : JPReticle::spacings()) {
        JMenuItem* item = m_spacingMenu->add(g, mm(v));
        item->setCheckable(true);
        item->onTriggered.connect([this, v] {
            JPReticle r = m_reticle;
            r.spacingMm = v;
            choose(r);
        });
        m_spacingItems.emplace_back(item, v);
    }
    for (double v : JPReticle::sizes()) {
        JMenuItem* item = m_sizeMenu->add(g, mm(v));
        item->setCheckable(true);
        item->onTriggered.connect([this, v] {
            JPReticle r = m_reticle;
            r.sizeMm = v;
            choose(r);
        });
        m_sizeItems.emplace_back(item, v);
    }
    m_spacingItem = m_menu->add(g, "Spacing", {}, m_spacingMenu.get());
    m_sizeItem    = m_menu->add(g, "Size", {}, m_sizeMenu.get());
    m_menu->addSeparator(g);
    m_infoItem = m_menu->add(g, "Show Image Info");
    m_infoItem->setCheckable(true);
    // The tick has already flipped when this runs.
    m_infoItem->onTriggered.connect([this] {
        setShowImageInfo(m_infoItem->isChecked());
        if (onShowImageInfoChanged) onShowImageInfoChanged(m_showInfo);
    });
    m_fitItem = m_menu->add(g, "Fit the Picture");
    m_fitItem->onTriggered.connect([this] {
        m_zoom = 1.0;
        invalidate();
    });
    setContextMenu(m_menu.get());
}

void JPCameraView::prepareContextMenu(float, float) {
    const bool calibrated = m_cal.valid;
    for (auto& [item, k] : m_kindItems) {
        item->setChecked(k == m_reticle.kind);
        JPReticle r;
        r.kind = k;
        item->setEnabled(calibrated || !r.toScale());
    }
    m_uncalibrated->setVisible(!calibrated);
    for (auto& [item, v] : m_spacingItems) item->setChecked(v == m_reticle.spacingMm);
    for (auto& [item, v] : m_sizeItems) item->setChecked(v == m_reticle.sizeMm);
    const bool lined = m_reticle.kind == JPReticle::Kind::Grid || m_reticle.kind == JPReticle::Kind::Ruler;
    const bool shaped = m_reticle.kind == JPReticle::Kind::Circle || m_reticle.kind == JPReticle::Kind::Square;
    m_spacingItem->setEnabled(calibrated && lined);
    m_sizeItem->setEnabled(calibrated && shaped);
    m_fitItem->setEnabled(m_zoom > 1.0);
    m_infoItem->setChecked(m_showInfo);
    m_nozzleHereItem->setVisible(bool(onMoveNozzleHere));
    for (auto& [item, z] : m_zoomItems) item->setChecked(z == m_sensitivity);
    for (auto& [item, q] : m_qualityItems) item->setChecked(q == m_quality);
    m_estimateZItem->setVisible(onEstimateZ && canEstimateZ && canEstimateZ());
}

const char* JPCameraView::name(RenderingQuality q) {
    switch (q) {
        case RenderingQuality::High:      return "High";
        case RenderingQuality::BestScale: return "BestScale";
        default:                          return "Low";
    }
}

void JPCameraView::setRenderingQuality(RenderingQuality q) {
    if (q == m_quality) return;
    m_quality = q;
    // Drawn again, sampled the new way.
    if (m_tex != kNullTexture) {
        if (m_showingStill) upload(m_still);
        else if (m_frame.width > 0) upload(m_frame);
    }
    invalidate();
}

void JPCameraView::upload(const JPFrame& picture) {
    dropTexture();
    m_tex = m_hal.uploadTexture(picture.rgba.data(), uint32_t(picture.width), uint32_t(picture.height),
                                m_quality == RenderingQuality::Low ? JTextureSampling::Sharp : JTextureSampling::Smooth);
    m_w = picture.width;
    m_h = picture.height;
}

const char* JPCameraView::name(ZoomSensitivity s) {
    switch (s) {
        case ZoomSensitivity::High: return "High";
        case ZoomSensitivity::Low:  return "Low";
        default:                    return "Medium";
    }
}

void JPCameraView::setShowImageInfo(bool on) {
    m_showInfo = on;
    invalidate();
}

void JPCameraView::setLight(bool has, std::optional<bool> on) {
    m_hasLight = has;
    m_lightOn = on;
    invalidate();
}

void JPCameraView::lightToggle(float& cx, float& cy, float& size) const {
    // At the top right of the picture as it is on screen.
    size = JStyle::current().controlHeight * kLightControls;
    cx = m_shown.x + m_shown.width - size * 0.5f;
    cy = m_shown.y + size * 0.5f;
}

bool JPCameraView::inLightToggle(float x, float y) const {
    if (!m_hasLight || m_selecting || onPicked || m_shown.width <= 0) return false;
    float cx, cy, size;
    lightToggle(cx, cy, size);
    return std::hypot(x - cx, y - cy) <= size * 0.5f;
}

void JPCameraView::drawLightToggle(JVectorCanvas& vg) const {
    const JStyle& st = JStyle::current();
    float cx, cy, size;
    lightToggle(cx, cy, size);
    // Pressed, it sinks a little, as OpenPnP's.
    if (m_lightPressed) {
        cx += st.borderWidth;
        cy += st.borderWidth;
    }
    const bool on = m_lightOn.value_or(false);
    const uint8_t* c = on ? Colors::Warning : Colors::MutedText;
    const JPaint ink = JPaint::solid(rgb(c[0], c[1], c[2]));
    const JPaint under = JPaint::solid(rgba(Colors::OverlayScrim[0], Colors::OverlayScrim[1], Colors::OverlayScrim[2],
                                            Colors::OverlayScrim[3]));
    vg.fillCircle(cx, cy, size * 0.5f, under);
    const float w = 2 * st.borderWidth;
    vg.strokeCircle(cx, cy, size * kSunDisc, w, ink);
    for (int i = 0; i < kRays; ++i) {
        const double a = (15.0 + 30.0 * i) * kPi / 180.0;
        vg.drawLine(cx + float(std::cos(a)) * size * kRayFrom, cy + float(std::sin(a)) * size * kRayFrom,
                    cx + float(std::cos(a)) * size * kRayTo, cy + float(std::sin(a)) * size * kRayTo, w, ink);
    }
}

void JPCameraView::drawImageInfo(JPrimitiveBuffer& buf, float x, float y) const {
    const JStyle& st = JStyle::current();
    double mean = 0;
    for (double i : m_intervals) mean += i;
    const double fps = m_intervals.empty() || mean <= 0 ? 0 : double(m_intervals.size()) / mean;
    char text[4][48];
    std::snprintf(text[0], sizeof text[0], "Resolution: %d x %d", m_w, m_h);
    std::snprintf(text[1], sizeof text[1], "Zoom: %d%%", int(m_zoom * 100));
    std::snprintf(text[2], sizeof text[2], "FPS: %.1f", fps);
    std::snprintf(text[3], sizeof text[3], "Histogram:");
    const float lh = JTextHelper::lineHeight(), pad = 2 * st.spacing;
    float tw = 256.f + 2;
    for (const auto& t : text) tw = std::max(tw, JTextHelper::measureWidth(t));
    const float boxW = tw + 2 * pad, boxH = 4 * lh + kHistogramHeight + 2 + 2 * pad;
    buf.pushRectangle(x, y, boxW, boxH, Colors::OverlayScrim, st.cornerRadius, st.borderWidth, Colors::ControlText);
    float ty = y + pad;
    for (const auto& t : text) {
        JTextHelper::pushText(buf, x + pad, ty, t, Colors::ControlText);
        ty += lh;
    }
    // Each channel's histogram, smoothed, and scaled so the tallest but the
    // two most extreme levels (saturation, typically) fills the height.
    if (m_frame.width != m_w || m_frame.height != m_h || m_frame.rgba.empty()) return;
    double h[3][256] = {};
    const size_t n = size_t(m_frame.width) * size_t(m_frame.height);
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c) h[c][m_frame.rgba[i * 4 + size_t(c)]] += 1;
    double most = 0;
    for (auto& channel : h) {
        double last = channel[0];
        for (int b = 1; b < 255; ++b) {
            const double now = channel[b];
            channel[b] = (now * 2 + last + channel[b + 1]) / 4;
            last = now;
        }
        double sorted[256];
        std::copy(std::begin(channel), std::end(channel), sorted);
        std::sort(std::begin(sorted), std::end(sorted));
        most = std::max(most, sorted[256 - 1 - 2]);
    }
    if (most <= 0) return;
    const float hx = x + pad + 1, hy = ty + 1;
    const uint8_t* colours[3] = { Colors::Danger, Colors::Success, Colors::Accent };
    for (int b = 0; b < 256; ++b) {
        // Tallest first, each channel's column down to the next; under all three, where they overlap, light.
        int order[3] = { 0, 1, 2 };
        std::sort(std::begin(order), std::end(order), [&](int a, int c) { return h[a][b] > h[c][b]; });
        float top = 0;
        for (int k = 0; k < 3; ++k) {
            const float v = float(std::min<double>(kHistogramHeight, h[order[k]][b] * kHistogramHeight / most));
            const float next = k < 2 ? float(std::min<double>(kHistogramHeight, h[order[k + 1]][b] * kHistogramHeight / most)) : 0.f;
            top = v;
            if (top > next) buf.pushRectangle(hx + float(b), hy + kHistogramHeight - top, 1, top - next, colours[order[k]], 0.f);
        }
        const float all = float(std::min<double>(kHistogramHeight, h[order[2]][b] * kHistogramHeight / most));
        if (all > 0) buf.pushRectangle(hx + float(b), hy + kHistogramHeight - all, 1, all, Colors::ControlText, 0.f);
    }
}

void JPCameraView::choose(const JPReticle& reticle) {
    m_reticle = reticle;
    invalidate();
    if (onReticleChanged) onReticleChanged(m_reticle);
}

void JPCameraView::setMarks(std::vector<Mark> marks) {
    m_marks = std::move(marks);
    invalidate();
}

void JPCameraView::setOverlay(const std::string& key, Overlay overlay) {
    if (overlay) m_overlays[key] = std::move(overlay);
    else m_overlays.erase(key);
    invalidate();
}

void JPCameraView::setReticle(const JPReticle& reticle) {
    m_reticle = reticle;
    invalidate();
}

void JPCameraView::setCalibration(const JPCameraCalibration& calibration) {
    m_cal = calibration;
    // How far the picture reaches from its middle: its farthest corner.
    m_reachMm = 0;
    if (m_cal.valid)
        for (const auto& [cx, cy] : { std::pair{ 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }) {
            double dx, dy;
            if (m_cal.mmForPixels((cx - 0.5) * m_cal.width, (cy - 0.5) * m_cal.height, dx, dy))
                m_reachMm = std::max(m_reachMm, std::hypot(dx, dy));
        }
    invalidate();
}

JPCameraView::~JPCameraView() {
    *m_alive = false;
    if (m_unwatch) m_unwatch();
    dropTexture();
}

void JPCameraView::dropTexture() {
    if (m_tex != kNullTexture) m_hal.releaseTexture(m_tex);
    m_tex = kNullTexture;
}

void JPCameraView::setFeed(JPCameraFeed* feed) {
    if (m_unwatch) m_unwatch();
    m_unwatch = nullptr;
    m_feed = feed;
    m_have = 0;
    dropTexture();
    if (feed) {
        std::weak_ptr<bool> alive = m_alive;
        m_unwatch = feed->onFrame.connect([this, alive](uint64_t) {
            JMainThreadDispatcher::instance().post([this, alive] {
                if (const auto a = alive.lock(); a && *a) showLatest();
            });
        });
    }
    invalidate();
}

void JPCameraView::clearPicture() {
    dropTexture();
    // Pictures already on their way (posted before the camera closed) not shown either.
    if (JPFrame last; m_feed && m_feed->latest(last, 0)) m_have = last.sequence;
    m_frame = JPFrame {};
    invalidate();
}

void JPCameraView::setMessage(const std::string& text) {
    m_message = text;
    invalidate();
}

void JPCameraView::setPrompt(const std::string& text) {
    m_prompt = text;
    invalidate();
}

void JPCameraView::showPicture(const JPFrame& picture, const std::string& text, int ms) {
    if (picture.width <= 0 || picture.height <= 0) return;
    m_still = picture;
    m_showingStill = true;
    upload(m_still);
    m_stillText = text;
    m_stillUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    invalidate();
    if (onPictureShown) onPictureShown();
}

void JPCameraView::showLatest() {
    // A picture shown in place of the live one, until its time is up.
    if (std::chrono::steady_clock::now() < m_stillUntil) return;
    m_stillText.clear();
    m_showingStill = false;
    // Held while the machine works (Suspend during tasks), and no oftener than the preview's rate.
    if (suspended && suspended()) return;
    const auto now0 = std::chrono::steady_clock::now();
    if (m_previewFps > 0 && m_lastShown != std::chrono::steady_clock::time_point {}
        && now0 - m_lastShown < std::chrono::duration<double>(1.0 / m_previewFps))
        return;
    // Posted frames can queue behind a busy main loop; take only the newest.
    if (!m_feed || !m_feed->latest(m_frame, m_have)) return;
    m_have = m_frame.sequence;
    m_lastShown = now0;
    const auto now = std::chrono::steady_clock::now();
    if (m_lastPicture != std::chrono::steady_clock::time_point {}) {
        m_intervals.push_back(std::chrono::duration<double>(now - m_lastPicture).count());
        if (m_intervals.size() > kFpsPictures) m_intervals.pop_front();
    }
    m_lastPicture = now;
    upload(m_frame);
    m_message.clear();
    invalidate();
}

void JPCameraView::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    m_drawnAt = std::chrono::steady_clock::now();
    const JRect b = bounds();
    const JStyle& st = JStyle::current();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::DockContentBg, 0.f);
    if (m_tex == kNullTexture || m_w <= 0 || m_h <= 0) {
        // No picture: OpenPnP's capture error picture in its place, fitted to the view as a picture is.
        const float scale = std::min(b.width / kErrorWidth, b.height / kErrorHeight);
        const float px = b.x + (b.width - kErrorWidth * scale) * 0.5f, py = b.y + (b.height - kErrorHeight * scale) * 0.5f;
        const JColor grey = rgb(kErrorGrey, kErrorGrey, kErrorGrey);
        buf.pushRectangle(px, py, kErrorWidth * scale, kErrorHeight * scale, grey.data(), 0.f);
        JVectorCanvas none;
        errorCross(none, px, py, scale);
        none.flush(buf);
        if (!m_message.empty()) {
            const float tw = JTextHelper::measureWidth(m_message);
            JTextHelper::pushText(buf, b.x + std::max(0.f, (b.width - tw) * 0.5f),
                                  b.y + (b.height - JTextHelper::lineHeight()) * 0.5f, m_message,
                                  Colors::MutedText, b.width);
        }
        return;
    }
    // Fit the picture, shape kept, centred; zoomed about the centre, so the
    // crosshair stays where the camera is looking, and cut to the view.
    float scale = std::min(b.width / float(m_w), b.height / float(m_h)) * float(m_zoom);
    // Best scale: a whole number of the view's pixels to a picture's pixel, or of them to one.
    if (m_quality == RenderingQuality::BestScale)
        scale = scale >= 1.f ? std::floor(scale) : 1.f / std::ceil(1.f / scale);
    const float w = float(m_w) * scale, h = float(m_h) * scale;
    const float x = b.x + (b.width - w) * 0.5f, y = b.y + (b.height - h) * 0.5f;
    buf.pushClip(b.x, b.y, b.width, b.height);
    // Straightened, the picture is drawn through the straightener's mesh: each
    // grid cell from where it is in the picture as taken (by the GPU where
    // there is one); cells the camera does not see are left out, bare.
    if (placing() && !(m_showingStill && m_still.straightened)) {
        const int cols = m_straight->columns(), rows = m_straight->rows();
        const auto& g = m_straight->grid();
        std::vector<JPrimitiveBuffer::JImageVertex> tris;
        tris.reserve(size_t(cols) * size_t(rows) * 6);
        auto node = [&](int c, int r) {
            const JPStraightener::Node& n = g[size_t(r) * size_t(cols + 1) + size_t(c)];
            return std::pair{ n, JPrimitiveBuffer::JImageVertex{ x + w * float(c) / float(cols), y + h * float(r) / float(rows), n.u, n.v } };
        };
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const auto [n00, v00] = node(c, r);
                const auto [n10, v10] = node(c + 1, r);
                const auto [n01, v01] = node(c, r + 1);
                const auto [n11, v11] = node(c + 1, r + 1);
                if (!n00.seen || !n10.seen || !n01.seen || !n11.seen) continue;
                tris.insert(tris.end(), { v00, v10, v01, v10, v11, v01 });
            }
        buf.pushImageMesh(m_tex, std::move(tris));
    } else {
        buf.pushImage(x, y, w, h, m_tex);
    }
    m_picX = x;
    m_picY = y;
    m_picScale = scale;

    // What of the picture is on screen: what is drawn over it stays on it.
    const float vx0 = std::max(x, b.x), vy0 = std::max(y, b.y);
    const float vx1 = std::min(x + w, b.x + b.width), vy1 = std::min(y + h, b.y + b.height);
    buf.popClip();
    buf.pushClip(vx0, vy0, vx1 - vx0, vy1 - vy0);
    m_shown = JRect { vx0, vy0, vx1 - vx0, vy1 - vy0 };

    // The reticle, the cross through the point the camera is looking at
    // first; in millimetres through the calibration for this picture size.
    JVectorCanvas vg;
    const JColor c = rgb(Colors::Accent[0], Colors::Accent[1], Colors::Accent[2]);
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, line = st.borderWidth;
    const bool calibrated = m_cal.valid && m_cal.width == m_w && m_cal.height == m_h;
    // At the height it looks at now (OpenPnP's viewing plane), to that scale, where the camera is calibrated
    // at two heights; else as calibrated.
    JPCameraCalibration seen = m_cal;
    if (calibrated && viewingPlaneZ)
        if (const std::optional<double> z = viewingPlaneZ()) seen = m_cal.atHeight(*z);
    JPReticle::Place place;
    if (calibrated)
        place = [&](double xMm, double yMm, float& sx, float& sy) {
            double rx, ry, px, py;
            if (!seen.pixelFor(xMm, yMm, 0, 0, rx, ry) || !shown(rx, ry, px, py)) return false;
            sx = x + float(px) * scale;
            sy = y + float(py) * scale;
            return true;
        };
    const double pxPerMm = calibrated ? (seen.scaleX() + seen.scaleY()) / 2 * scale : 0;
    m_reticle.draw(vg, JRect{ vx0, vy0, vx1 - vx0, vy1 - vy0 }, cx, cy, place, pxPerMm, m_reachMm, line,
                   st.spacing, kLeastGap * st.spacing, JPaint::solid(c));
    if (place)
    {
        if (!m_taskUnderway)
            for (const auto& [key, overlay] : m_overlays) overlay(vg, place, line);
        else if (m_taskOverlay)
            m_taskOverlay(vg, place, line);
    }
    // Marks in the picture's own pixels (calibrated or not).
    for (const Mark& mk : m_marks) {
        float sx, sy;
        if (!selectionCorner(mk.x, mk.y, sx, sy)) continue;
        const uint8_t* tone = mk.found ? Colors::Success : Colors::Danger;
        const JPaint ink = JPaint::solid(rgb(tone[0], tone[1], tone[2]));
        const float r = float(mk.diameter * 0.5) * scale;
        vg.strokeCircle(sx, sy, r, 2 * line, ink);
        if (mk.cross) {
            vg.drawLine(sx - r * 0.5f, sy, sx + r * 0.5f, sy, 2 * line, ink);
            vg.drawLine(sx, sy - r * 0.5f, sx, sy + r * 0.5f, 2 * line, ink);
        }
    }

    // The selection: its outline and a handle at each corner, its size by it.
    if (m_selecting && m_selection.width > 0 && m_selection.height > 0) {
        const JColor sc = rgb(Colors::Warning[0], Colors::Warning[1], Colors::Warning[2]);
        const double xs[] = { double(m_selection.x), double(m_selection.x + m_selection.width) };
        const double ys[] = { double(m_selection.y), double(m_selection.y + m_selection.height) };
        float px[4], py[4];
        bool all = true;
        const int order[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        for (int i = 0; i < 4; ++i) all = all && selectionCorner(xs[order[i][0]], ys[order[i][1]], px[i], py[i]);
        if (all) {
            for (int i = 0; i < 4; ++i) vg.drawLine(px[i], py[i], px[(i + 1) % 4], py[(i + 1) % 4], line, JPaint::solid(sc));
            const float half = st.spacing;
            for (int i = 0; i < 4; ++i) vg.strokeRect(px[i] - half, py[i] - half, 2 * half, 2 * half, line, JPaint::solid(sc));
        }
    }

    // A drag to look somewhere: from the cross to where the camera will look.
    if (m_dragging) {
        const float half = JTextHelper::lineHeight() * 0.5f;
        const JColor d = rgb(Colors::Warning[0], Colors::Warning[1], Colors::Warning[2]);
        vg.drawLine(cx, cy, m_dragX, m_dragY, line, JPaint::solid(d));
        vg.strokeRect(m_dragX - half, m_dragY - half, 2 * half, 2 * half, line, JPaint::solid(d));
    }

    // OpenPnP's rotation handle: where the rotation is now (grey); held, the ring, where it would turn to
    // (the text colour) and a cross turned with it to line things up by.
    if (float rcx, rcy, ring, handle; rotationRing(rcx, rcy, ring, handle)) {
        const JColor grey = rgb(Colors::MutedText[0], Colors::MutedText[1], Colors::MutedText[2]);
        const JColor held = rgb(Colors::ControlText[0], Colors::ControlText[1], Colors::ControlText[2]);
        float hx, hy;
        ringPoint(rcx, rcy, ring, *handleRotation(), hx, hy);
        vg.strokeCircle(hx, hy, handle, line, JPaint::solid(grey));
        if (m_rotating) {
            vg.strokeCircle(rcx, rcy, ring, line, JPaint::solid(grey));
            const double to = ringRotation(rcx, rcy, m_rotX, m_rotY);
            float tx, ty;
            ringPoint(rcx, rcy, ring, to, tx, ty);
            vg.strokeCircle(tx, ty, handle, line, JPaint::solid(held));
            for (const double across : { 0.0, 90.0 }) {
                float ax, ay, bx, by;
                ringPoint(rcx, rcy, ring, to + across, ax, ay);
                ringPoint(rcx, rcy, ring, to + across + 180.0, bx, by);
                vg.drawLine(ax, ay, bx, by, line, JPaint::solid(held));
            }
        }
    }

    // A live camera not calibrated for its picture size: OpenPnP's red X in the picture's top left, as on its
    // capture error picture (in proportion, the picture taken as 640 wide), as it cannot be measured through
    // (its scale, its lens, where it is all unknown), with what is missing said beside it.
    if (m_warnUncalibrated && !calibrated && !m_showingStill && !m_taskUnderway) errorCross(vg, x, y, w / kErrorWidth);

    // The light toggle, while not choosing a place or a selection (as OpenPnP's).
    if (m_hasLight && !m_selecting && !onPicked) drawLightToggle(vg);

    vg.flush(buf);

    // A picture left from before the camera was lost: say so over it, or it
    // would pass for a live one.
    const float lh = JTextHelper::lineHeight(), pad = st.spacing;
    if (!m_message.empty()) {
        buf.pushRectangle(vx0, vy0, vx1 - vx0, lh + 2 * pad, Colors::OverlayScrim, 0.f);
        JTextHelper::pushText(buf, vx0 + pad, vy0 + pad, m_message, Colors::Warning, vx1 - vx0 - 2 * pad);
    }
    // Not calibrated: said, at the top, unless something more pressing is.
    if (m_warnUncalibrated && !calibrated && !m_showingStill && !m_taskUnderway && m_message.empty() && m_prompt.empty()) {
        char text[96];
        std::snprintf(text, sizeof text, "Not calibrated for its %d\xC3\x97%d pictures: calibrate it (the target button)", m_w, m_h);
        buf.pushRectangle(vx0, vy0, vx1 - vx0, lh + 2 * pad, Colors::OverlayScrim, 0.f);
        JTextHelper::pushText(buf, vx0 + pad, vy0 + pad, text, Colors::Danger, vx1 - vx0 - 2 * pad);
    }
    // What to do (choose a place), over the top.
    if (!m_prompt.empty() && m_message.empty()) {
        buf.pushRectangle(vx0, vy0, vx1 - vx0, lh + 2 * pad, Colors::OverlayScrim, 0.f);
        JTextHelper::pushText(buf, vx0 + pad, vy0 + pad, m_prompt, Colors::ControlText, vx1 - vx0 - 2 * pad);
    }
    // A picture shown in place of the live one: what it is, at its foot.
    if (!m_stillText.empty()) {
        const float ty = vy1 - lh - 2 * pad;
        buf.pushRectangle(vx0, ty, vx1 - vx0, lh + 2 * pad, Colors::OverlayScrim, 0.f);
        JTextHelper::pushText(buf, vx0 + pad, ty + pad, m_stillText, Colors::ControlText, vx1 - vx0 - 2 * pad);
    }
    // The selection's size, at its top left.
    if (m_selecting && m_selection.width > 0 && m_selection.height > 0) {
        float sx, sy;
        if (selectionCorner(m_selection.x, m_selection.y, sx, sy)) {
            char text[48];
            std::snprintf(text, sizeof text, "%d x %d", m_selection.width, m_selection.height);
            const float tw = JTextHelper::measureWidth(text);
            const float ty = std::max(vy0, sy - lh - 2 * pad);
            buf.pushRectangle(sx, ty, tw + 2 * pad, lh + 2 * pad, Colors::OverlayScrim, 0.f);
            JTextHelper::pushText(buf, sx + pad, ty + pad, text, Colors::Warning);
        }
    }
    // Zoomed: by how much, in the bottom corner.
    if (m_zoom > 1.0) {
        char text[32];
        std::snprintf(text, sizeof text, "%.0f%%", m_zoom * 100.0);
        const float tw = JTextHelper::measureWidth(text);
        const float zx = vx0, zy = vy1 - lh - 2 * pad;
        buf.pushRectangle(zx, zy, tw + 2 * pad, lh + 2 * pad, Colors::OverlayScrim, 0.f);
        JTextHelper::pushText(buf, zx + pad, zy + pad, text, Colors::ControlText);
    }
    // The image info, while nothing is said over the picture.
    if (m_showInfo && m_message.empty() && m_stillText.empty() && m_prompt.empty()) drawImageInfo(buf, vx0 + pad, vy0 + pad);
    buf.popClip();
}

bool JPCameraView::handleScroll(float, float, float wheel) {
    if (wheel == 0.f) return false;
    // At best scale only whole steps show: a notch zooms by 2 at least.
    const double perNotch = m_quality == RenderingQuality::BestScale ? std::max(2.0, zoomPerNotch(m_sensitivity))
                                                                     : zoomPerNotch(m_sensitivity);
    const double z = std::clamp(m_zoom * std::pow(perNotch, double(wheel)), 1.0, kMostZoom);
    // Back near fitted is fitted, not 99.99% of it.
    m_zoom = z < 1.0 + 1e-6 ? 1.0 : z;
    invalidate();
    return true;
}

void JPCameraView::setSelectionEnabled(bool on) {
    m_selecting = on;
    m_selDragging = false;
    invalidate();
}

void JPCameraView::setSelection(const Selection& s) {
    m_selection = s;
    invalidate();
}

std::shared_ptr<JPFrame> JPCameraView::captureSelection() const {
    const Selection s = m_selection;
    if (s.width <= 0 || s.height <= 0 || m_frame.width <= 0) return nullptr;
    const int x0 = std::clamp(s.x, 0, m_frame.width), y0 = std::clamp(s.y, 0, m_frame.height);
    const int x1 = std::clamp(s.x + s.width, 0, m_frame.width), y1 = std::clamp(s.y + s.height, 0, m_frame.height);
    if (x1 <= x0 || y1 <= y0) return nullptr;
    auto out = std::make_shared<JPFrame>();
    out->width = x1 - x0;
    out->height = y1 - y0;
    out->captured = m_frame.captured;
    out->rgba.reserve(size_t(out->width) * size_t(out->height) * 4);
    for (int y = y0; y < y1; ++y) {
        const uint8_t* row = &m_frame.rgba[(size_t(y) * size_t(m_frame.width) + size_t(x0)) * 4];
        out->rgba.insert(out->rgba.end(), row, row + size_t(out->width) * 4);
    }
    return out;
}

bool JPCameraView::selectionCorner(double rawX, double rawY, float& sx, float& sy) const {
    double x, y;
    if (m_picScale <= 0 || !shown(rawX, rawY, x, y)) return false;
    sx = m_picX + float(x) * m_picScale;
    sy = m_picY + float(y) * m_picScale;
    return true;
}

void JPCameraView::dragSelection(double px, double py) {
    const Selection f = m_selFrom;
    const int dx = int(std::lround(px - m_selFromX)), dy = int(std::lround(py - m_selFromY));
    int x0 = f.x, y0 = f.y, x1 = f.x + f.width, y1 = f.y + f.height;
    switch (m_selCorner) {
        case -1: x0 += dx; x1 += dx; y0 += dy; y1 += dy; break;
        case -2: x0 = int(std::lround(m_selFromX)); y0 = int(std::lround(m_selFromY)); x1 = int(std::lround(px)); y1 = int(std::lround(py)); break;
        case 0: x0 += dx; y0 += dy; break;
        case 1: x1 += dx; y0 += dy; break;
        case 2: x1 += dx; y1 += dy; break;
        case 3: x0 += dx; y1 += dy; break;
    }
    m_selection = { std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0) };
    invalidate();
}

void JPCameraView::handleMousePress(float x, float y) {
    if (m_selecting) {
        double px, py;
        if (!pixelAt(x, y, px, py)) return;
        m_selDragging = true;
        m_selFromX = px;
        m_selFromY = py;
        m_selFrom = m_selection;
        // A corner within a handle of the press is held; inside, it moves; else a new one.
        const Selection& s = m_selection;
        const double cx[] = { double(s.x), double(s.x + s.width), double(s.x + s.width), double(s.x) };
        const double cy[] = { double(s.y), double(s.y), double(s.y + s.height), double(s.y + s.height) };
        const float grab = 2 * JStyle::current().spacing;
        m_selCorner = -2;
        for (int i = 0; i < 4 && s.width > 0; ++i) {
            float sx, sy;
            if (selectionCorner(cx[i], cy[i], sx, sy) && std::abs(sx - x) <= grab && std::abs(sy - y) <= grab) {
                m_selCorner = i;
                break;
            }
        }
        if (m_selCorner == -2 && px >= s.x && py >= s.y && px <= s.x + s.width && py <= s.y + s.height) m_selCorner = -1;
        return;
    }
    // The light toggle: switched when let go over it.
    if (inLightToggle(x, y)) {
        m_lightPressed = true;
        invalidate();
        return;
    }
    // The rotation handle: turned when let go.
    if (float rcx, rcy, ring, handle; rotationRing(rcx, rcy, ring, handle)) {
        float hx, hy;
        ringPoint(rcx, rcy, ring, *handleRotation(), hx, hy);
        if (std::hypot(x - hx, y - hy) <= handle) {
            m_rotating = true;
            m_rotX = x;
            m_rotY = y;
            invalidate();
            return;
        }
    }
    // A place being chosen: the click is it.
    if (onPicked) {
        double px, py;
        if (pixelAt(x, y, px, py)) onPicked(px, py);
        return;
    }
    m_pressed  = true;
    m_dragging = false;
    m_dragX = x;
    m_dragY = y;
    if (JWidget::s_shiftDown || JWidget::s_doubleClick) {
        m_pressed = false;
        lookAt(x, y);
    }
}

void JPCameraView::handleMouseMove(float x, float y) {
    if (m_selDragging) {
        if (!JWidget::s_leftDown) {
            m_selDragging = false;
            return;
        }
        // Off the picture, it stops at the edge.
        const float cx = std::clamp(x, m_picX, m_picX + float(m_w) * m_picScale - 1);
        const float cy = std::clamp(y, m_picY, m_picY + float(m_h) * m_picScale - 1);
        double px, py;
        if (pixelAt(cx, cy, px, py)) dragSelection(px, py);
        return;
    }
    if (m_rotating) {
        if (!JWidget::s_leftDown) m_rotating = false;   // let go where this view did not hear it: not turned
        m_rotX = x;
        m_rotY = y;
        invalidate();
        return;
    }
    if (!m_pressed) return;
    // The button let go where this view did not hear it: no move.
    if (!JWidget::s_leftDown) {
        m_pressed = m_dragging = false;
        invalidate();
        return;
    }
    const float slop = JStyle::current().doubleClickSlop;
    if (!m_dragging && std::abs(x - m_dragX) <= slop && std::abs(y - m_dragY) <= slop) return;
    m_dragging = true;
    m_dragX = x;
    m_dragY = y;
    invalidate();
}

void JPCameraView::handleMouseRelease(float x, float y) {
    if (m_lightPressed) {
        m_lightPressed = false;
        invalidate();
        if (inLightToggle(x, y) && onToggleLight) onToggleLight();
        return;
    }
    if (m_selDragging) {
        m_selDragging = false;
        return;
    }
    if (m_rotating) {
        m_rotating = false;
        invalidate();
        if (float rcx, rcy, ring, handle; rotationRing(rcx, rcy, ring, handle) && onRotateTo)
            onRotateTo(ringRotation(rcx, rcy, x, y));
        return;
    }
    const bool dragged = m_pressed && m_dragging;
    m_pressed = m_dragging = false;
    if (!dragged) return;
    invalidate();
    const JRect b = bounds();
    if (x >= b.x && y >= b.y && x < b.x + b.width && y < b.y + b.height) lookAt(x, y);
}

bool JPCameraView::rotationRing(float& cx, float& cy, float& ring, float& handle) const {
    if (!handleRotation || m_selecting || onPicked || !handleRotation()) return false;
    // On the picture as it is on screen, so all of it is seen.
    const JRect& b = m_shown;
    cx = b.x + b.width * 0.5f;
    cy = b.y + b.height * 0.5f;
    ring = std::min(b.width, b.height) * 0.5f * kRingShare;
    handle = JStyle::current().controlHeight * kHandleControls;
    return ring > handle;
}

void JPCameraView::ringPoint(float cx, float cy, float ring, double deg, float& x, float& y) {
    // As OpenPnP's: on screen (y down), 0 up and counter-clockwise.
    const double a = -(deg + 90.0) * kPi / 180.0;
    x = cx + ring * float(std::cos(a));
    y = cy + ring * float(std::sin(a));
}

double JPCameraView::ringRotation(float cx, float cy, float x, float y) {
    double a = std::atan2(double(y - cy), double(x - cx)) * 180.0 / kPi;
    if (JWidget::s_altDown) a = std::round(a / kSnapDeg) * kSnapDeg;
    return std::remainder(-(a + 90.0), 360.0);
}

bool JPCameraView::pixelAt(float x, float y, double& px, double& py) const {
    if (m_picScale <= 0 || m_w <= 0) return false;
    px = (x - m_picX) / m_picScale;
    py = (y - m_picY) / m_picScale;
    if (px < 0 || py < 0 || px >= m_w || py >= m_h) return false;
    // Straightened, the pixel clicked is back to the picture as taken.
    if (const JPStraightener* s = placing()) {
        double rx, ry;
        if (!s->toRaw(px, py, rx, ry)) return false;
        px = rx;
        py = ry;
    }
    return true;
}

void JPCameraView::lookAt(float x, float y) {
    double px, py;
    if (onLookAt && pixelAt(x, y, px, py)) onLookAt(px, py);
}

bool JPCameraView::shown(double rawX, double rawY, double& x, double& y) const {
    if (const JPStraightener* s = placing())
        return s->toStraight(rawX, rawY, x, y) && x >= 0 && y >= 0 && x < m_w && y < m_h;
    x = rawX;
    y = rawY;
    return true;
}

const JPStraightener* JPCameraView::placing() const {
    if (!m_straight || m_straight->width() != m_w || m_straight->height() != m_h) return nullptr;
    return m_straightShown || (m_showingStill && m_still.straightened) ? m_straight.get() : nullptr;
}

} // inline namespace jf
