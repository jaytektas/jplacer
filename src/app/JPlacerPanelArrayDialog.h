// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "model/JPPanelLocation.h"
#include "ui/JPPlacementsViewer.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's Panel Array Generator (Panels tab, Create array of
// children…): copies of a child laid out in rows and columns (alternate
// rows may have a column more or less, and be offset), or round a centre
// in angular and radial steps; named "<id>[row,column]", the child itself
// becoming "[1,1]". Each change is shown at once on the panel below; OK
// keeps the copies, Cancel takes them away.
class JPlacerPanelArrayDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 800, kH = 600;

    // `onDone`: after OK, the panel's children changed.
    JPlacerPanelArrayDialog(JPConfiguration& config, const JPJob* job, JPPanelLocation& panel,
                            JPPlacementsHolderLocation& child, std::function<void()> onDone, JGpuHal& hal, int sx,
                            int sy, NativeWinHandleType parent);
    ~JPlacerPanelArrayDialog();

protected:
    void layout(float w, float h) override;

private:
    void generate();
    void removeCopies();
    void restoreRoot();
    void finish();
    void showPage();
    std::unique_ptr<JPPlacementsHolderLocation> copyOfRoot() const;

    JPConfiguration&                          m_config;
    const JPJob*                              m_job;
    JPPanelLocation&                          m_panel;
    JPPlacementsHolderLocation&               m_child;
    std::string                               m_childId;
    std::function<void()>                     m_onDone;
    bool                                      m_done = false;
    std::vector<JPPlacementsHolderLocation*>  m_copies;

    bool     m_circular = false;
    int      m_columns = 1, m_rows = 1, m_angular = 1, m_radial = 1, m_alternateDelta = 0;
    JPLength m_columnStep { 10, JPLengthUnit::Millimeters }, m_rowStep { 10, JPLengthUnit::Millimeters };
    JPLength m_alternateOffset { 0, JPLengthUnit::Millimeters };
    JPLength m_centerX { 0, JPLengthUnit::Millimeters }, m_centerY { 0, JPLengthUnit::Millimeters };
    bool     m_proportional = false, m_panelFrame = true;

    std::unique_ptr<JContainer>               m_top;
    // The content's parts, owned here and laid out by m_top as the type chooses.
    std::unique_ptr<JWidget>                  m_typeRow, m_rectangular, m_round;
    std::unique_ptr<JPPlacementsViewer>       m_viewer;
    std::unique_ptr<JDialogButtonBox>         m_buttons;
};

} // inline namespace jf
