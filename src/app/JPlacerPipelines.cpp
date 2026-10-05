// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPipelines.h"

#include "JPlacerChoiceDialog.h"
#include "JPlacerClassSelectionDialog.h"
#include "JPlacerPipelineEditorDialog.h"

#include "camera/JPCameraFeed.h"
#include "openpnp/JPXmlWriter.h"
#include "tasks/JPCameraLook.h"

#include <opencv2/imgproc.hpp>

inline namespace jf {

JPlacerPipelines::JPlacerPipelines(JAppWindow& window, JPlacerMachine& machine) : m_window(window), m_machine(machine) {}

void JPlacerPipelines::useHeadCamera(JPPipeline& pipeline, const std::string& directory) const {
    JPPipeline::Context& ctx = pipeline.context();
    ctx.configurationDirectory = directory;
    JPCameraFeed* feed = m_machine.headCameraFeed();
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

} // inline namespace jf
