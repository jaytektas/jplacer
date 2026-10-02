// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPCameraView.h"
#include "JPChoiceRow.h"

#include "camera/JPCameraFeed.h"
#include "machine/JPCellConfig.h"

#include <j/core/JButton.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>
#include <j/core/JSlider.h>

#include <functional>
#include <memory>
#include <vector>

inline namespace jf {

// The cell's cameras: choose one, see it live. Only the camera shown runs —
// the others are stopped — so a camera nobody is looking at costs nothing.
class JPCameraPanel : public JContainer {
public:
    // Where a camera is looking on the machine, for a simulated camera to draw.
    using ViewFor = std::function<std::function<bool(double&, double&)>(const JPCameraConfig&)>;

    // A camera's calibration (not valid when it has none), for straightening its picture.
    using CalibrationFor = std::function<JPCameraCalibration(const std::string& cameraId)>;

    // `capturesDir`: where Save Picture writes. `viewFor`: each camera's view.
    JPCameraPanel(JSceneGraph& graph, JGpuHal& hal, const JPCellConfig& cell, std::string capturesDir,
                  const ViewFor& viewFor, CalibrationFor calibrationFor);

    // How the picture is shown: straightened or as taken, and how much of a
    // straightened one (JPStraightener's showAll). Set by the owner; changed
    // here, reported to onViewChanged.
    void setView(bool straight, double showAll);
    std::function<void(bool straight, double showAll)> onViewChanged;
    // A calibration changed: straighten by it from now on.
    void refreshStraightening();
    ~JPCameraPanel() override;

    // The camera shown changed (its id), and the one before it (empty at
    // first): the owner switches the cameras' lights.
    std::function<void(const std::string& shown, const std::string& before)> onShown;

    // Calibrate and Visual Test were pressed: the owner runs them on the
    // camera shown.
    // The live picture double-clicked at this pixel: the owner looks there.
    std::function<void(double px, double py)> onLookAtPixel;
    std::function<void()> onCalibrate;
    std::function<void()> onVisualTest;

    void show(size_t index);
    // Show the camera with this id; false when there is none.
    bool showCamera(const std::string& id);
    // The camera shown (its id), empty when there is none.
    std::string shownId() const;
    // The camera shown, running; null when there is none.
    JPCameraFeed* shownFeed() const;
    // While a task drives the camera: its buttons, and the choice of camera,
    // are off (switching camera would stop the one it is looking through).
    void setBusy(bool busy);
    // Marks over the live picture (see JPCameraView::setMarks).
    void setMarks(std::function<std::vector<JPViewMark>()> marks);
    // A word about the picture beside the mode (e.g. why it is dark).
    void setNote(const std::string& text);
    // Write the shown camera's latest picture to capturesDir. The file
    // written, or empty with the reason in the note.
    std::string savePicture();

private:
    std::vector<std::unique_ptr<JPCameraFeed>> m_feeds;
    JPCameraView*                              m_view  = nullptr;
    JPChoiceRow*                               m_choice = nullptr;
    JPChoiceRow*                               m_viewChoice = nullptr;
    JSlider*                                   m_edges = nullptr;
    CalibrationFor                             m_calibrationFor;
    bool                                       m_straight = false;
    double                                     m_showAll = 0;
    std::vector<JButton*>                      m_taskButtons;
    JLabel*                                    m_state = nullptr;
    JLabel*                                    m_note  = nullptr;
    size_t                                     m_shown = SIZE_MAX;
    std::string                                m_capturesDir;
    std::vector<std::function<void()>>         m_unwatch;
    std::shared_ptr<bool>                      m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
