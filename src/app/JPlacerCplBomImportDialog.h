// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "import/JPCplBomImport.h"
#include "import/JPMappingProfiles.h"
#include "ui/JPGroupFrame.h"

#include <j/app/JDialogWindow.h>
#include <j/core/JButton.h>
#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDataGrid.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JScrollArea.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// Import a board from its CAD files (DESIGN.md, Import), as an assembly
// house takes them: the placement file (CPL) and any BOMs and other tables.
//  * Files: each file, what it is, the profile its columns were read by,
//    its rows; Choose Placement File…, Add BOM or Table…, Remove.
//  * Columns of the file chosen: each column's header, what it holds (a
//    choice of jplacer's fields, guessed from the header, or a profile's),
//    and its first values; the file's units, its profile, Save Profile….
//  * What it makes, as the choices are changed: placements, parts (from the
//    library, made for it on Import, not chosen yet), what is in one file only,
//    and each field the files disagree on with which file's to take.
//  * Create Missing Parts; Cancel and Import.
// Import builds the board (JPCplBomImport) and hands it to `onImported`.
class JPlacerCplBomImportDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 1080, kH = 720;

    JPlacerCplBomImportDialog(JPConfiguration& config, std::function<void(JPBoard&)> onImported, JGpuHal& hal, int sx,
                              int sy, NativeWinHandleType parent);

protected:
    void layout(float w, float h) override;

private:
    void addFile(JPImportSource::Role role);
    void addSource(const std::string& path, JPImportSource::Role role);
    void removeSource();
    void choose(int source);
    void fillFiles();
    void fillColumns();
    void fillSummary();
    void saveProfile();
    void runImport();
    std::string profilesFile() const;

    std::shared_ptr<bool>        m_alive = std::make_shared<bool>(true);   // a file chooser's answer after it closed: dropped
    JPConfiguration&             m_config;
    std::function<void(JPBoard&)> m_onImported;
    JPCplBomImport               m_import;
    JPMappingProfiles            m_profiles;
    int                          m_chosen = 0;
    // Remade after the event that changed them, not inside it (a choice's own signal would destroy it).
    bool m_refillColumns = false, m_refillSummary = false, m_refillFiles = false;

    std::unique_ptr<JContainer>       m_content;
    JDataGrid*                        m_files = nullptr;
    JButton*                          m_cplButton = nullptr;
    JButton*                          m_addButton = nullptr;
    JButton*                          m_removeButton = nullptr;
    JPGroupFrame*                     m_columnsFrame = nullptr;
    JComboBox*                        m_role = nullptr;
    JComboBox*                        m_units = nullptr;
    JComboBox*                        m_profile = nullptr;
    JScrollArea*                      m_columns = nullptr;
    JScrollArea*                      m_summary = nullptr;
    JCheckBox*                        m_create = nullptr;
    std::unique_ptr<JDialogButtonBox> m_buttons;
    JButton*                          m_importButton = nullptr;
};

} // inline namespace jf
