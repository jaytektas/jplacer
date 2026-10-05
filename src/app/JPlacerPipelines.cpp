// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPipelines.h"

#include "JPlacerChoiceDialog.h"
#include "JPlacerClassSelectionDialog.h"
#include "JPlacerPipelineEditorDialog.h"

#include "camera/JPCameraFeed.h"
#include "openpnp/JPXmlWriter.h"
#include "pipeline/JPPipelineParameter.h"
#include "pipeline/JPStageUtil.h"
#include "setup/JPVisionPipelines.h"
#include "tasks/JPCameraLook.h"
#include "tasks/JPVisionPipelinePrep.h"

#include <j/core/Dialog.h>
#include <j/core/FrameTimer.h>

#include <opencv2/imgproc.hpp>

inline namespace jf {

JPlacerPipelines::JPlacerPipelines(JAppWindow& window, JPlacerMachine& machine) : m_window(window), m_machine(machine) {}

void JPlacerPipelines::useHeadCamera(JPPipeline& pipeline, const std::string& directory) const {
    useCamera(pipeline, m_machine.headCameraFeed(), directory);
}

void JPlacerPipelines::useCamera(JPPipeline& pipeline, JPCameraFeed* feed, const std::string& directory) const {
    JPPipeline::Context& ctx = pipeline.context();
    ctx.configurationDirectory = directory;
    if (!feed) return;
    // The camera's picture, settled first unless told to skip it.
    ctx.capture = [feed](const std::string& settle, const std::string&, cv::Mat& bgr, std::string& why) {
        if (settle != "Skip") {
            JPGrayImage ignored;
            if (!JPCameraLook::settled(*feed, ignored, why)) return false;
        }
        JPFrame frame;
        if (!feed->latest(frame, 0) || frame.width <= 0) {
            why = feed->config().name + " gives no picture";
            return false;
        }
        cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data());
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
        return true;
    };
    JPFrame frame;
    if (!feed->latest(frame, 0) || frame.width <= 0) return;
    ctx.cameraWidth = frame.width;
    ctx.cameraHeight = frame.height;
    // Its scale and places, when it is calibrated for these pictures.
    const JPCell* cell = m_machine.cell();
    if (!cell) return;
    const JPCameraCalibration cal = cell->cameraCalibration(feed->config().id, frame.width, frame.height);
    if (!cal.valid) return;
    ctx.pixelsPerMmX = cal.scaleX();
    ctx.pixelsPerMmY = cal.scaleY();
    if (feed != m_machine.headCameraFeed()) return;
    ctx.locationToPixel = [this, cal](double x, double y, double& px, double& py) {
        const JPlacerMachine::Where at = m_machine.whereIs(JPSetupForm::Tool::Camera);
        if (!at[0] || !at[1]) return false;
        return cal.pixelFor(x, y, *at[0], *at[1], px, py);
    };
}

void JPlacerPipelines::edit(const std::string& title, std::shared_ptr<JPPipeline> pipeline, std::function<void(const JPPipeline&)> keep,
                            const std::string& original) {
    const std::string before = original.empty() ? JPXmlWriter::text(pipeline->toXml()) : original;
    JPlacerPipelineEditorDialog::Owner owner;
    owner.x = m_window.window().screenX();
    owner.y = m_window.window().screenY();
    owner.width = int(m_window.width());
    owner.height = int(m_window.height());
    auto chooseClass = [this](const std::string& t, const std::string& description, std::vector<std::string> classes,
                              std::function<void(std::string)> chosen) {
        m_window.openModal<JPlacerClassSelectionDialog>(t, description, std::move(classes), std::move(chosen));
    };
    auto closed = [this, title, pipeline, keep, before](bool dirty) {
        if (!dirty) return;
        m_window.openModal<JPlacerChoiceDialog>("Closing Pipeline Editor!", "Save pipeline changes?",
                                                std::vector<std::string> { "Yes", "No", "Cancel" }, 2,
                                                [this, title, pipeline, keep, before](int chosen) {
                                                    if (chosen == 0) keep(*pipeline);
                                                    else if (chosen == 2) edit(title, pipeline, keep, before);
                                                });
    };
    m_window.openModal<JPlacerPipelineEditorDialog>(title, pipeline, before, owner, chooseClass, closed);
}

namespace {

// How long a preview picture is shown, and the next one after it.
constexpr int kPreviewShownMs = 3000, kPreviewNextMs = 1000;

JPFrame frameOf(const cv::Mat& image, const std::string& colorSpace) {
    JPFrame f;
    cv::Mat rgba;
    std::string why;
    if (image.empty() || !JPStageUtil::toRgba(image, colorSpace, true, rgba, why)) return f;
    f.width = rgba.cols;
    f.height = rgba.rows;
    f.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    return f;
}

} // namespace

std::shared_ptr<JPPipeline> JPlacerPipelines::prepared(JPConfiguration& config, const JPVisionSettings& settings,
                                                       const std::string& partId, const std::string& packageId, JPCameraFeed*& feed,
                                                       std::string& why) const {
    const bool bottom = settings.kind == JPVisionSettings::Kind::Bottom;
    feed = bottom ? m_machine.upCameraFeed() : m_machine.headCameraFeed();
    auto p = std::make_shared<JPPipeline>(JPVisionPipelines::of(settings));
    useCamera(*p, feed, config.directory());
    if (!bottom) {
        JPVisionPipelinePrep::fiducial(*p, config, settings, partId, packageId, 0,
                                       m_machine.cell() ? m_machine.cell()->config().vision.fiducialMaxDistanceMm : JPVisionConfig {}.fiducialMaxDistanceMm);
        return p;
    }
    const double angle = m_machine.cell() ? m_machine.cell()->config().vision.testAlignmentAngle : 0;
    std::string part = partId, pkg = packageId;
    if (part.empty() && pkg.empty() && chosenPart) part = chosenPart();
    if (part.empty() && pkg.empty() && chosenPackage) pkg = chosenPackage();
    if (!JPVisionPipelinePrep::bottom(*p, config, settings, part, pkg, angle, why)) return nullptr;
    return p;
}

void JPlacerPipelines::editVision(JPConfiguration& config, const std::string& settingsId, const std::string& partId,
                                  const std::string& packageId, std::function<void()> kept) {
    const JPVisionSettings* v = config.visionSettings(settingsId);
    if (!v) return;
    JPCameraFeed* feed = nullptr;
    std::string why;
    const auto p = prepared(config, *v, partId, packageId, feed, why);
    if (!p) {
        JDialog::message("Error", why);
        return;
    }
    const bool bottom = v->kind == JPVisionSettings::Kind::Bottom;
    edit(bottom ? "Bottom Vision Pipeline" : "Fiducial Vision Pipeline", p, [&config, settingsId, kept](const JPPipeline& edited) {
        if (JPVisionSettings* s = config.visionSettings(settingsId)) {
            JPVisionPipelines::set(*s, edited);
            if (kept) kept();
        }
    });
}

void JPlacerPipelines::previewVision(JPConfiguration& config, const std::string& settingsId, const std::string& partId,
                                     const std::string& packageId, const std::string& parameter) {
    const JPVisionSettings* v = config.visionSettings(settingsId);
    if (!v) return;
    JPCameraFeed* feed = nullptr;
    std::string why;
    const auto p = prepared(config, *v, partId, packageId, feed, why);
    if (!p) return;
    const std::vector<JPPipelineParameter> params = JPPipelineParameter::of(*p);
    const auto param = std::find_if(params.begin(), params.end(), [&](const JPPipelineParameter& x) { return x.name() == parameter; });
    if (param == params.end() || (param->effectStage().empty() && !param->previewResult())) return;
    if (!p->process(why)) return;
    std::vector<JPFrame> pictures;
    if (!param->effectStage().empty())
        if (const JPPipeline::Result* r = p->result(param->effectStage())) pictures.push_back(frameOf(r->image, r->colorSpace));
    if (param->previewResult()) pictures.push_back(frameOf(p->workingImage(), p->workingColorSpace()));
    JPCameraView* view = m_machine.cameraViewOf(feed);
    if (!view || pictures.empty()) return;
    const JPPipelineValue* assigned = p->property(parameter);
    const std::string caption = param->label() + " = " + param->display(assigned ? *assigned : param->defaultValue());
    const int preview = ++m_previews;
    view->showPicture(pictures.front(), caption, kPreviewShownMs);
    // The next a second on, unless another preview came meanwhile.
    for (size_t i = 1; i < pictures.size(); ++i)
        JFrameTimer::singleShot(float(kPreviewNextMs * int(i)), [this, view, picture = pictures[i], caption, preview] {
            if (preview == m_previews) view->showPicture(picture, caption, kPreviewShownMs);
        });
}

} // inline namespace jf
