// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraView.h"
#include "JPIconButton.h"

#include "camera/JPCameraFeed.h"
#include "machine/JPCameraConfig.h"

#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// One camera, live, in a dock of its own: its picture (as taken or
// straightened), and a line saying what it is doing; its tools (As Taken,
// Save Picture, Calibrate, Visual Test, its settings) are icons for the
// dock's tab (tabTools). The camera runs while its panel is on screen (the front tab, or
// torn out into a window) and stops when it is not, so a camera nobody can see
// costs nothing; the owner switches its light with it (onRunning).
class JPCameraPanel : public JContainer {
public:
    // A camera's calibration for pictures width x height (not valid when it
    // has none), for straightening its picture.
    using CalibrationFor = std::function<JPCameraCalibration(const std::string& cameraId, int width, int height)>;

    // `capturesDir`: where Save Picture writes. `view`: where the camera is
    // looking, for a simulated camera to draw (null for a real one).
    JPCameraPanel(JSceneGraph& graph, JGpuHal& hal, const JPCameraConfig& camera, std::string capturesDir,
                  std::function<bool(double&, double&)> view, CalibrationFor calibrationFor);
    ~JPCameraPanel() override;

    const JPCameraConfig& camera() const { return m_feed.config(); }
    JPCameraFeed& feed() { return m_feed; }
    // Running: on screen, and the camera giving pictures.
    bool isRunning() const { return m_feed.isRunning(); }

    // The camera started (on screen) or stopped (off it): its light follows.
    std::function<void(bool running)> onRunning;
    // Calibrate and Visual Test pressed: the owner runs them on this camera.
    std::function<void()> onCalibrate;
    std::function<void()> onVisualTest;
    // Its settings asked for: the owner shows the camera in Machine Setup.
    std::function<void()> onSettings;
    // The live picture double-clicked at this pixel (of the picture as taken).
    std::function<void(double px, double py)> onLookAtPixel;

    // How the picture is shown: straightened or as taken (how much of a
    // straightened one's edge shows is the camera's setting, showAll). Set
    // by the owner; changed here, reported to onViewChanged.
    void setView(bool straight);
    std::function<void(bool straight)> onViewChanged;
    // The reticle over the picture (JPReticle): set by the owner, kept from
    // last time; chosen from the picture's right-click menu, reported to
    // onReticleChanged.
    void setReticle(const JPReticle& reticle);
    std::function<void(const JPReticle&)> onReticleChanged;
    // A calibration changed (or the camera opened, at a size): straighten and
    // draw reticles by it from now on.
    void refreshStraightening();

    // While a task drives the camera: its buttons are off, and it runs even
    // off screen.
    void setBusy(bool busy);
    // Marks over the live picture (see JPCameraView::setMarks).
    void setMarks(std::function<std::vector<JPViewMark>()> marks);
    // A word about the picture (what a task is doing, why it is dark).
    void setNote(const std::string& text);
    // Write the latest picture to capturesDir. The file written, or empty
    // with the reason in the note.
    std::string savePicture();
    // Its tools, as icon buttons for the dock's tab (each JPIconButton::size()
    // wide), in order. Owned here.
    std::vector<JWidget*> tabTools() const;

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

private:
    // Off screen this long, the camera is stopped.
    static constexpr int kHiddenMs = 500;

    void start();
    void stopIfHidden();

    JPCameraFeed                          m_feed;
    JPCameraView*                         m_view = nullptr;
    std::unique_ptr<JPIconButton>         m_asTaken, m_save, m_calibrate, m_visualTest, m_settings;
    JLabel*                               m_state = nullptr;
    JLabel*                               m_note  = nullptr;
    CalibrationFor                        m_calibrationFor;
    bool                                  m_busy = false;
    bool                                  m_straight = false;
    std::string                           m_capturesDir;
    std::chrono::steady_clock::time_point m_drawn;   // last drawn: on screen until shortly after
    std::vector<std::function<void()>>    m_unwatch;
    std::shared_ptr<bool>                 m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
