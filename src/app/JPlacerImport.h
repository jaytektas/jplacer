// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerLayout.h"

#include "import/JPBoardBuilder.h"
#include "ui/JPImportPanel.h"

#include <j/core/DockWidget.h>

#include <memory>
#include <set>

inline namespace jf {

// The Import dock: the open job's board sources, read and reviewed before
// anything reaches the job. The pick-and-place file (and the CAD tool it is
// read as, asked every time it is chosen), the BOM, the origin and the outline
// are changed here; each change is shown at once (the board drawn as it
// would be, the report: counts, the importer's notes, what changes from the
// job's board, where the file and the BOM disagree) and held until Accept;
// Discard goes back to the job's. A source changed on disk since it was read
// says so, and is read again only when asked. Accepting keeps what was set
// on placements that still hold (JPBoardChanges::merge) and finds parts for
// those that need them.
class JPlacerImport {
public:
    static constexpr const char* kTitle = "Import";

    JPlacerImport(JPlacerJob& job, JSceneGraph& graph, JPlacerLayout& layout);
    ~JPlacerImport();

    void showDock();
    // The dock shown and a pick-and-place file asked for (File > Import, the
    // Board panel's Import).
    void choosePlacements();

private:
    void fromJob();
    void rebuild();
    void show();
    void accept();
    void setSource(JPSource::Kind kind, const std::string& path, JPCadTool::Kind tool);
    void reread(JPSource::Kind kind);
    JPSource* pending(JPSource::Kind kind);
    const JPSource* inJob(JPSource::Kind kind) const;
    bool differs() const;

    JPlacerJob&                    m_job;
    JPlacerLayout&                 m_layout;
    int                            m_watch = 0;
    std::unique_ptr<JPImportPanel> m_panel;
    std::unique_ptr<JDockWidget>   m_dock;
    // What would be accepted.
    std::vector<JPSource>          m_sources;
    JPBoardFrame                   m_frame;
    std::set<std::string>          m_takeBom;
    JPBoardBuilder::Result         m_built;
    bool                           m_ok = false;
    std::string                    m_error;
    std::vector<std::string>       m_lineKeys;   // each report line's disagreement key, or empty
    std::shared_ptr<bool>          m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
