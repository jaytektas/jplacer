// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPBoard.h"
#include "model/JPConfiguration.h"
#include "model/JPPanelLocation.h"
#include "ui/JPPlacementsTableModel.h"
#include "ui/JPTable.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JRadioButton.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's ChildFiducialSelectorDialog (Panels tab, Use Children
// Fiducials…): every fiducial and placement of the panel's boards and
// panels, as it lies on the panel; shown are the fiducials, the
// placements, or both, and with Hull Only those on the outermost edge.
// Auto Select chooses the four (on each side) spanning the most area. OK
// makes the chosen ones the panel's pseudo-placements.
class JPlacerChildFiducialsDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 800, kH = 600;

    // `onChosen` has the unique ids chosen ("Brd1⇒FID1").
    JPlacerChildFiducialsDialog(JPConfiguration& config, JPPanelLocation& panel,
                                std::function<void(std::vector<std::string>)> onChosen, JGpuHal& hal, int sx, int sy,
                                NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    enum class Types { Fiducials, Placements, Both };

    void gather(JPPanelLocation& at);
    void update();
    std::vector<JPPlacement> filtered() const;
    std::vector<JPPlacement> hull(const std::vector<JPPlacement>& from) const;
    std::vector<std::string> good() const;

    JPPanelLocation&                              m_panel;
    std::function<void(std::vector<std::string>)> m_onChosen;
    std::vector<JPPlacement>                      m_all;
    JPBoard                                       m_shown;   // the rows: the list shown
    JPPlacementsTableModel                        m_model;
    Types                                         m_types = Types::Fiducials;
    bool                                          m_hullOnly = true;
    std::unique_ptr<JLabel>                       m_instructions;
    std::unique_ptr<JContainer>                   m_options;
    std::unique_ptr<JPTable>                      m_table;
    std::unique_ptr<JDialogButtonBox>             m_buttons;
    JButton*                                      m_ok = nullptr;
    std::vector<JRadioButton*>                    m_radios;
};

} // inline namespace jf
