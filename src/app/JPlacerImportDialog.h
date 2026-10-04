// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPBoardImporter.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JCheckBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLineEdit.h>

#include <functional>
#include <memory>
#include <vector>

inline namespace jf {

// A board importer's dialog, as OpenPnP's: titled with the importer's
// description; a Files group (each file's label, its path, Browse), an
// Options group (its tick boxes, as they start), and Cancel and Import.
// Import reads the files: a failure is shown ("Import Error") and the
// dialog stays; else it closes and `onImported` has the placements read.
// Escape cancels.
class JPlacerImportDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 560, kH = 400;

    JPlacerImportDialog(const JPBoardImporter& importer, JPConfiguration& config,
                        std::function<void(JPBoard&)> onImported, JGpuHal& hal, int sx, int sy,
                        NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    void runImport();

    const JPBoardImporter&            m_importer;
    JPConfiguration&                  m_config;
    std::function<void(JPBoard&)>     m_onImported;
    std::unique_ptr<JContainer>       m_content;
    std::vector<JLineEdit*>           m_files;
    std::vector<JCheckBox*>           m_options;
    std::unique_ptr<JDialogButtonBox> m_buttons;
};

} // inline namespace jf
