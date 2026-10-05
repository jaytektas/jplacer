// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelineResultsPanel.h"

#include "JPUiParts.h"

#include "common/JPlacerLog.h"
#include "pipeline/JPStageRegistry.h"
#include "pipeline/JPStageUtil.h"

#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <sstream>

inline namespace jf {

namespace {

// The picture over what was found, four fifths of the height.
constexpr float kViewSplit = 0.8f;
// How near the mouse must be to what was found, in pixels (isModelAtPoint).
constexpr double kNear = 5;

// What was found at a pixel: a rectangle's, key point's or circle's centre near it.
std::string modelAt(const JPPipelineModel& m, int x, int y) {
    auto near = [x, y](double cx, double cy) { return std::abs(x - cx) < kNear && std::abs(y - cy) < kNear; };
    JPPipelineModel one;
    if (const auto* r = std::get_if<cv::RotatedRect>(&m.value); r && near(r->center.x, r->center.y)) one.value = *r;
    else if (const auto* k = std::get_if<cv::KeyPoint>(&m.value); k && near(k->pt.x, k->pt.y)) one.value = *k;
    else if (const auto* c = std::get_if<JPPipelineModel::Circle>(&m.value); c && near(c->x, c->y)) one.value = *c;
    else if (const auto* rs = std::get_if<std::vector<cv::RotatedRect>>(&m.value)) {
        for (const auto& r2 : *rs)
            if (near(r2.center.x, r2.center.y)) {
                one.value = r2;
                break;
            }
    } else if (const auto* ks = std::get_if<std::vector<cv::KeyPoint>>(&m.value)) {
        for (const auto& k2 : *ks)
            if (near(k2.pt.x, k2.pt.y)) {
                one.value = k2;
                break;
            }
    } else if (const auto* cs = std::get_if<std::vector<JPPipelineModel::Circle>>(&m.value)) {
        for (const auto& c2 : *cs)
            if (near(c2.x, c2.y)) {
                one.value = c2;
                break;
            }
    }
    if (one.empty()) return {};
    // A single key point describes itself as a list of one; shown as it.
    std::string s = one.describe();
    if (s.size() > 2 && s.front() == '[' && s.back() == ']') s = s.substr(1, s.size() - 2);
    return s;
}

// The model as the editor's text: a list one to a line.
std::string modelText(const JPPipelineModel& m) {
    std::string s = m.describe();
    const bool list = std::visit([](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        return std::is_same_v<T, std::vector<cv::RotatedRect>> || std::is_same_v<T, std::vector<JPPipelineModel::Circle>>
               || std::is_same_v<T, std::vector<cv::KeyPoint>> || std::is_same_v<T, std::vector<JPPipelineModel::Line>>
               || std::is_same_v<T, std::vector<JPPipelineModel::TemplateMatch>> || std::is_same_v<T, std::vector<cv::Point2d>>;
    }, m.value);
    if (!list || s.size() < 2) return s;
    // "[a, b]": one to a line, each ended (as OpenPnP's).
    std::string out;
    int depth = 0;
    for (size_t i = 1; i + 1 < s.size(); ++i) {
        const char c = s[i];
        if (c == '[' || c == '{') ++depth;
        if (c == ']' || c == '}') --depth;
        if (c == ',' && depth == 0) {
            out += '\n';
            if (i + 1 < s.size() && s[i + 1] == ' ') ++i;
            continue;
        }
        out += c;
    }
    return out.empty() ? out : out + "\n";
}

// OpenPnP's length units' short names.
std::string unitShort(const std::string& unit) {
    static const std::pair<const char*, const char*> units[] = { { "Meters", "m" }, { "Centimeters", "cm" }, { "Millimeters", "mm" },
                                                                 { "Feet", "'" },   { "Inches", "\"" },     { "Mils", "mil" },
                                                                 { "Microns", "μm" } };
    for (const auto& [n, s] : units)
        if (unit == n) return s;
    return unit;
}

double mmPer(const std::string& unit) {
    static const std::pair<const char*, double> units[] = { { "Meters", 1000 }, { "Centimeters", 10 }, { "Millimeters", 1 },
                                                            { "Feet", 304.8 },  { "Inches", 25.4 },    { "Mils", 0.0254 },
                                                            { "Microns", 0.001 } };
    for (const auto& [n, mm] : units)
        if (unit == n) return mm;
    return 1;
}

} // namespace

JPPipelineResultsPanel::JPPipelineResultsPanel(JSceneGraph& graph, JGpuHal* hal, JPPipeline& pipeline)
    : JContainer(graph, 0.f, 0.f), m_pipeline(pipeline) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    // The stage's name and times, in the middle.
    auto title = JPUiParts::row(graph);
    graph.getLayout(title->getNodeId()).justifyContent = JJustifyContent::Center;
    m_name = title->add(std::make_unique<JLabel>(graph, ""));
    m_name->setMinWidthFollowsText(true);
    title->setFixedSize(0.f, st.labelHeight);
    add(std::move(title));

    // The tools, in the middle.
    auto bar = JPUiParts::row(graph);
    graph.getLayout(bar->getNodeId()).justifyContent = JJustifyContent::Center;
    auto tool = [&](const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, tip, icon, tip));
    };
    m_first = tool("nav-first", "First pipeline stage.");
    m_first->onClicked.connect([this] { showStage(0); });
    m_previous = tool("nav-previous", "Previous pipeline stage.");
    m_previous->onClicked.connect([this] { showStage(shown() - 1); });
    m_next = tool("nav-next", "Next pipeline stage.");
    m_next->onClicked.connect([this] { showStage(shown() + 1); });
    m_last = tool("nav-last", "Last pipeline stage.");
    m_last->onClicked.connect([this] { showStage(int(m_pipeline.stages().size()) - 1); });
    bar->add(std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size()));
    m_pin = tool("pin_disabled", "Pin pipeline stage output.");
    m_pin->onClicked.connect([this] {
        if (m_pinned < 0) {
            m_pinned = m_selected;
            m_pin->setIcon("pin_enabled");
        } else {
            m_pinned = -1;
            m_pin->setIcon("pin_disabled");
            update();
        }
    });
    bar->add(std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size()));
    m_color = tool("color-true", "Images displayed in true color.");
    m_color->onClicked.connect([this] {
        m_trueColors = !m_trueColors;
        m_color->setIcon(m_trueColors ? "color-true" : "color-false");
        m_color->setTooltip(m_trueColors ? "Images displayed in true color."
                                         : "Images displayed assuming BGR color space - colors may not look correct.");
        update();
    });
    bar->setFixedSize(0.f, JPIconButton::size());
    add(std::move(bar));

    // The picture and its status over what was found.
    m_viewPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_viewPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_view = m_viewPane->add(std::make_unique<JPMatView>(graph, hal));
    m_view->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_view->onHover = [this](int x, int y) { hover(x, y); };
    m_status = m_viewPane->add(std::make_unique<JLabel>(graph, " "));
    m_status->setFixedSize(0.f, st.labelHeight);
    m_status->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_modelPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_modelPane->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_model = m_modelPane->add(std::make_unique<JPTextView>(graph));
    m_model->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_viewPane.get(), kViewSplit);
    m_split->addPane(m_modelPane.get(), 1 - kViewSplit);
}

JPPipelineResultsPanel::~JPPipelineResultsPanel() = default;

void JPPipelineResultsPanel::refresh() {
    const int n = int(m_pipeline.stages().size());
    // No stages, nothing chosen; the chosen one gone, the first.
    if (n == 0) {
        m_selected = m_pinned = -1;
    } else if (m_selected < 0 || m_selected >= n) {
        m_selected = 0;
        m_pinned = -1;
    } else if (m_pinned >= n) {
        m_pinned = -1;
    }
    if (m_pinned < 0) m_pin->setIcon("pin_disabled");
    update();
}

void JPPipelineResultsPanel::setSelectedStage(int index) {
    m_selected = index;
    update();
}

void JPPipelineResultsPanel::showStage(int index) {
    const int n = int(m_pipeline.stages().size());
    if (index < 0 || index >= n) return;
    (m_pinned >= 0 ? m_pinned : m_selected) = index;
    update();
}

void JPPipelineResultsPanel::update() {
    const int n = int(m_pipeline.stages().size());
    const int at = shown();
    const JPPipeline::Result* result = at >= 0 && at < n ? m_pipeline.result(m_pipeline.stages()[size_t(at)].name()) : nullptr;
    std::shared_ptr<JPFrame> frame;
    if (result && !result->image.empty()) {
        cv::Mat rgba;
        std::string why;
        if (JPStageUtil::toRgba(result->image, result->colorSpace, m_trueColors, rgba, why)) {
            frame = std::make_shared<JPFrame>();
            frame->width = rgba.cols;
            frame->height = rgba.rows;
            frame->rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
        } else {
            JLOGC(JPlacerLog::kPipeline, JLogLevel::Error) << why;
        }
    }
    m_view->setImage(frame);
    m_model->setText(result ? modelText(result->model) : std::string());
    char times[96] = "";
    if (result) std::snprintf(times, sizeof times, " ( %g ms / %g ms)", result->milliseconds, m_pipeline.totalMilliseconds());
    m_name->setText(result ? m_pipeline.stages()[size_t(at)].name() + times : std::string());
    const bool chosen = m_selected >= 0;
    m_first->setEnabled(chosen && at > 0);
    m_previous->setEnabled(chosen && at > 0);
    m_next->setEnabled(chosen && at < n - 1);
    m_last->setEnabled(chosen && at < n - 1);
}

void JPPipelineResultsPanel::hover(int x, int y) {
    const int n = int(m_pipeline.stages().size());
    const int at = shown();
    const JPPipeline::Result* result = at >= 0 && at < n ? m_pipeline.result(m_pipeline.stages()[size_t(at)].name()) : nullptr;
    if (result) {
        const std::string found = modelAt(result->model, x, y);
        if (!found.empty()) {
            m_status->setText(found);
            return;
        }
    }
    const JPFrame* f = m_view->image();
    if (!f) return;
    // The colour as shown, and in full-range HSV.
    const uint8_t* px = &f->rgba[(size_t(y) * size_t(f->width) + size_t(x)) * 4];
    cv::Mat rgb(1, 1, CV_8UC3, cv::Scalar(px[2], px[1], px[0])), hsv;
    cv::cvtColor(rgb, hsv, cv::COLOR_BGR2HSV_FULL);
    const cv::Vec3b h = hsv.at<cv::Vec3b>(0, 0);
    // A stage working in lengths (AffineWarp): where that is from the camera's centre, Y up.
    std::string aux;
    if (at >= 0 && at < n)
        if (const JPStageType* type = JPStageRegistry::instance().find(m_pipeline.stages()[size_t(at)].className());
            type && type->property("length-unit") && m_pipeline.context().pixelsPerMmX > 0) {
            const std::string unit = m_pipeline.stages()[size_t(at)].text("length-unit");
            const double per = mmPer(unit);
            char buf[96];
            std::snprintf(buf, sizeof buf, " (%f, %f %s)", (x - f->width / 2) / m_pipeline.context().pixelsPerMmX / per,
                          (-y + f->height / 2) / m_pipeline.context().pixelsPerMmY / per, unitShort(unit).c_str());
            aux = buf;
        }
    char buf[160];
    std::snprintf(buf, sizeof buf, "RGB: %03d, %03d, %03d HSV(full): %03d, %03d, %03d XY: %d, %d %s", px[0], px[1], px[2], h[0], h[1],
                  h[2], x, y, aux.c_str());
    m_status->setText(buf);
}

} // inline namespace jf
