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
#include "tasks/JPPipelineCamera.h"
#include "tasks/JPVisionPipelinePrep.h"

#include <j/core/Dialog.h>
#include <j/core/FrameTimer.h>
#include <j/core/MainThreadDispatcher.h>

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
    // Its calibration for the pictures it takes, when it has one.
    JPCameraCalibration cal;
    JPFrame frame;
    const bool taking = feed->latest(frame, 0) && frame.width > 0;
    if (const JPCell* cell = m_machine.cell(); cell && taking)
        cal = cell->cameraCalibration(feed->config().id, frame.width, frame.height);
    // Its pictures, straightened where it is calibrated, as a job's pipelines get them; places in them only for the
    // head's camera, where the head is.
    JPPipelineCamera::View view;
    if (feed == m_machine.headCameraFeed())
        view = [this](double& x, double& y) {
            const JPlacerMachine::Where at = m_machine.whereIs(JPSetupForm::Tool::Camera);
            if (!at[0] || !at[1]) return false;
            x = *at[0];
            y = *at[1];
            return true;
        };
    JPPipelineCamera::give(ctx, *feed, cal, view);
    // The camera kept running while the pipeline can take its picture (an editor open, a preview going): run on
    // the main thread, its picture cannot wait for the camera to be drawn back on screen. Let go of when the
    // pipeline (and every copy of it) is gone, on the main thread.
    static int held = 0;
    const std::string who = "pipeline " + std::to_string(++held), cameraId = feed->config().id;
    m_machine.keepCameraRunning(cameraId, who, true);
    std::shared_ptr<void> hold(nullptr, [machine = &m_machine, alive = m_machine.alive(), cameraId, who](void*) {
        JMainThreadDispatcher::instance().post([machine, alive, cameraId, who] {
            if (const auto a = alive.lock(); a && *a) machine->keepCameraRunning(cameraId, who, false);
        });
    });
    ctx.capture = [take = std::move(ctx.capture), hold](const std::string& settle, const std::string& light, cv::Mat& bgr,
                                                         std::string& why) { return take(settle, light, bgr, why); };
    if (!cal.valid && taking) {   // not calibrated: its size still known
        ctx.cameraWidth = frame.width;
        ctx.cameraHeight = frame.height;
    }
}

void JPlacerPipelines::edit(const std::string& title, std::shared_ptr<JPPipeline> pipeline, std::function<void(const JPPipeline&)> keep,
                            const std::string& original) {
    const std::string before = original.empty() ? pipeline->storedText() : original;
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
                                                    if (chosen == 0) keep(pipeline->stored());   // as kept: not as it ran
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
    // On the nozzle chosen, with the tip on it.
    const JPNozzleTipConfig* tip = nullptr;
    if (const JPCell* c = m_machine.cell())
        for (const JPNozzleConfig& n : c->config().nozzles)
            if (n.id == m_machine.chosenNozzleId())
                for (const JPNozzleTipConfig& t : c->config().nozzleTips)
                    if (t.id == n.tipId) tip = &t;
    if (!JPVisionPipelinePrep::bottom(*p, config, settings, part, pkg, angle, tip, feed ? &feed->config() : nullptr, why))
        return nullptr;
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
    for (JPFrame& f : pictures) f.straightened = true;   // the pipeline's pictures (JPPipelineCamera)
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
