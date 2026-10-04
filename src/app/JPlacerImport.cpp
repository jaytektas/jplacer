// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerImport.h"

#include "job/JPBoardChanges.h"
#include "library/JPPartMatcher.h"

#include <j/core/Dialog.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>

inline namespace jf {

namespace {

constexpr int kStatusMs = 8000;

std::vector<std::string> toolNames() {
    std::vector<std::string> out = { "(say which)" };
    for (const JPCadTool::Kind k : JPCadTool::all()) out.push_back(JPCadTool::name(k));
    return out;
}

std::string mm(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

std::string fileName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

} // namespace

JPlacerImport::JPlacerImport(JPlacerJob& job, JSceneGraph& graph, JPlacerLayout& layout) : m_job(job), m_layout(layout) {
    m_panel = std::make_unique<JPImportPanel>(graph, toolNames());
    m_panel->onAccept = [this] { accept(); };
    m_panel->onDiscard = [this] {
        fromJob();
        rebuild();
    };
    m_panel->onChoosePlacements = [this] { choosePlacements(); };
    m_panel->onRereadPlacements = [this] { reread(JPSource::Kind::Placements); };
    m_panel->onTool = [this](int i) {
        JPSource* s = pending(JPSource::Kind::Placements);
        if (!s || i < 1) return;
        s->tool = JPCadTool::all()[size_t(i - 1)];
        s->fingerprint.clear();   // read as another tool: the board changes
        rebuild();
    };
    m_panel->onChooseBom = [this] {
        std::weak_ptr<bool> alive = m_alive;
        JDialog::openFile("Choose the BOM", { "csv", "txt" }, [this, alive](std::string path) {
            if (const auto a = alive.lock(); a && *a) setSource(JPSource::Kind::Bom, path, JPCadTool::Kind::Other);
        });
    };
    m_panel->onRereadBom = [this] { reread(JPSource::Kind::Bom); };
    m_panel->onRemoveBom = [this] {
        std::erase_if(m_sources, [](const JPSource& s) { return s.kind == JPSource::Kind::Bom; });
        m_takeBom.clear();
        rebuild();
    };
    m_panel->onOrigin = [this](double x, double y) {
        m_frame.originX = x;
        m_frame.originY = y;
        rebuild();
    };
    m_panel->onOriginAtParts = [this] {
        if (!m_ok || m_built.board.placements.empty()) return;
        double x = std::numeric_limits<double>::max(), y = x;
        for (const JPPlacement& p : m_built.board.placements) {
            x = std::min(x, p.x);
            y = std::min(y, p.y);
        }
        m_frame.originX += x;
        m_frame.originY += y;
        rebuild();
    };
    m_panel->onOutline = [this](double w, double h) {
        m_frame.width = std::max(0.0, w);
        m_frame.height = std::max(0.0, h);
        rebuild();
    };
    m_panel->onOutlineAroundParts = [this] {
        if (!m_ok || m_built.board.placements.empty()) return;
        double w = 0, h = 0;
        for (const JPPlacement& p : m_built.board.placements) {
            w = std::max(w, p.x);
            h = std::max(h, p.y);
        }
        m_frame.width = w;
        m_frame.height = h;
        rebuild();
    };
    m_panel->onNoOutline = [this] {
        m_frame.width = m_frame.height = 0;
        rebuild();
    };
    m_panel->onReportLine = [this](int i) {
        if (i < 0 || size_t(i) >= m_lineKeys.size() || m_lineKeys[size_t(i)].empty()) return;
        const std::string& k = m_lineKeys[size_t(i)];
        if (!m_takeBom.erase(k)) m_takeBom.insert(k);
        rebuild();
    };
    m_dock = std::make_unique<JDockWidget>(kTitle, 0.f, 0.f, 0.f, 0.f);
    m_dock->setContent(m_panel.get());
    m_layout.add(m_dock.get(), JPlacerLayout::Home::Work);
    m_watch = m_job.watch([this](JPlacerJob::Change what) {
        if (what == JPlacerJob::Change::Board) {
            fromJob();
            rebuild();
        }
    });
    fromJob();
    rebuild();
}

JPlacerImport::~JPlacerImport() {
    *m_alive = false;
    m_job.unwatch(m_watch);
    m_layout.remove(m_dock.get());
    m_dock->setContent(nullptr);
}

void JPlacerImport::showDock() {
    m_layout.show(m_dock.get());
}

void JPlacerImport::fromJob() {
    const JPJob& j = m_job.job();
    m_sources = j.sources;
    m_frame = j.frame;
    m_takeBom = std::set<std::string>(j.bomChoices.begin(), j.bomChoices.end());
}

JPSource* JPlacerImport::pending(JPSource::Kind kind) {
    for (JPSource& s : m_sources)
        if (s.kind == kind) return &s;
    return nullptr;
}

const JPSource* JPlacerImport::inJob(JPSource::Kind kind) const {
    for (const JPSource& s : m_job.job().sources)
        if (s.kind == kind) return &s;
    return nullptr;
}

void JPlacerImport::choosePlacements() {
    showDock();
    std::weak_ptr<bool> alive = m_alive;
    JDialog::openFile("Choose the Pick-and-Place File", { "csv", "txt", "pos" }, [this, alive](std::string path) {
        if (const auto a = alive.lock(); !a || !*a) return;
        // The CAD tool is asked every time: chosen next, on the panel.
        setSource(JPSource::Kind::Placements, path, JPCadTool::Kind::Other);
        if (JPSource* s = pending(JPSource::Kind::Placements)) s->fingerprint = "?";
        rebuild();
    });
}

void JPlacerImport::setSource(JPSource::Kind kind, const std::string& path, JPCadTool::Kind tool) {
    if (JPSource* s = pending(kind)) *s = JPSource { kind, path, tool, {} };
    else m_sources.push_back({ kind, path, tool, {} });
    if (kind == JPSource::Kind::Bom) m_takeBom.clear();
    rebuild();
}

void JPlacerImport::reread(JPSource::Kind kind) {
    if (JPSource* s = pending(kind)) s->fingerprint.clear();   // what is on disk now
    rebuild();
}

bool JPlacerImport::differs() const {
    const JPJob& j = m_job.job();
    if (m_sources.size() != j.sources.size()) return true;
    for (size_t i = 0; i < m_sources.size(); ++i) {
        const JPSource& a = m_sources[i];
        const JPSource& b = j.sources[i];
        if (a.kind != b.kind || a.path != b.path || a.tool != b.tool || a.fingerprint != b.fingerprint) return true;
    }
    if (m_frame.originX != j.frame.originX || m_frame.originY != j.frame.originY || m_frame.width != j.frame.width
        || m_frame.height != j.frame.height)
        return true;
    return m_takeBom != std::set<std::string>(j.bomChoices.begin(), j.bomChoices.end());
}

void JPlacerImport::rebuild() {
    m_ok = false;
    m_error.clear();
    m_built = JPBoardBuilder::Result();
    const JPSource* cpl = pending(JPSource::Kind::Placements);
    if (!differs()) {
        // Nothing asked for: the job's board as it was read, the files not read again.
        m_built.board = m_job.job().board;
        m_ok = true;
    } else if (cpl && cpl->fingerprint == "?") {
        m_error = "Say which CAD tool wrote " + fileName(cpl->path) + ": it decides how its bottom side is read.";
    } else if (cpl) {
        // A source read now: what it holds now; else as it was read.
        for (JPSource& s : m_sources)
            if (s.fingerprint.empty()) s.fingerprint = JPSource::fingerprintOf(s.path);
        m_ok = JPBoardBuilder::build(m_sources, m_frame, m_takeBom, m_built, m_error);
    }
    show();
}

void JPlacerImport::show() {
    const JPJob& j = m_job.job();
    // The sources, and whether each has changed on disk since it was read.
    auto source = [&](JPSource::Kind kind) {
        JPImportPanel::Source out;
        const JPSource* s = pending(kind);
        if (!s) return out;
        out.file = fileName(s->path);
        out.canReread = true;
        const std::string now = JPSource::fingerprintOf(s->path);
        if (now.empty()) out.state = "cannot be read now";
        else if (!s->fingerprint.empty() && s->fingerprint != "?" && s->fingerprint != now)
            out.state = "changed on disk since it was read: Read Again to see what changes";
        else if (const JPSource* was = inJob(kind); was && (was->path != s->path || was->fingerprint != s->fingerprint))
            out.state = "read now: Accept to take it into the job";
        return out;
    };
    const JPSource* cpl = pending(JPSource::Kind::Placements);
    int tool = 0;
    if (cpl && cpl->fingerprint != "?")
        for (size_t i = 0; i < JPCadTool::all().size(); ++i)
            if (JPCadTool::all()[i] == cpl->tool) tool = int(i + 1);
    m_panel->showSources(source(JPSource::Kind::Placements), tool, source(JPSource::Kind::Bom));
    m_panel->showFrame(m_frame.originX, m_frame.originY, m_frame.width, m_frame.height);

    std::vector<std::string> lines;
    m_lineKeys.clear();
    auto line = [&](const std::string& text, const std::string& key = {}) {
        lines.push_back(text);
        m_lineKeys.push_back(key);
    };
    std::vector<JPBoardChanges::Change> changes;
    if (!m_ok) {
        m_panel->showBoard(j.board, j.frame);
        if (!m_error.empty()) line("\xE2\x9A\xA0 " + m_error);
        else if (!cpl) line("Choose the board's pick-and-place file.");
    } else {
        const JPBoard& b = m_built.board;
        m_panel->showBoard(b, m_frame);
        int top = 0, bottom = 0, fids = 0, tht = 0, dnp = 0;
        double minX = 0, minY = 0;
        for (const JPPlacement& p : b.placements) {
            (p.side == JPPlacement::Side::Bottom ? bottom : top) += 1;
            fids += p.fiducial;
            tht += p.mounting == JPPlacement::Mounting::ThroughHole;
            dnp += p.doNotPlace;
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
        }
        line(std::to_string(b.placements.size()) + " placements: " + std::to_string(top) + " top, " + std::to_string(bottom) + " bottom");
        line(std::to_string(fids) + " fiducials, " + std::to_string(tht) + " through-hole, " + std::to_string(dnp) + " not to be placed");
        if (minX < 0 || minY < 0)
            line("\xE2\x9A\xA0 Placements lie left of or below the origin (to " + mm(minX) + ", " + mm(minY)
                 + " mm): the origin is not the board's bottom-left corner");
        for (const std::string& n : m_built.notes) line(n);
        if (!j.board.placements.empty() && differs()) {
            changes = JPBoardChanges::compare(j.board, b);
            line("\xE2\x80\x94 Changes from the job's board \xE2\x80\x94");
            if (changes.empty()) line("none");
            for (const auto& c : changes) line(std::string(JPBoardChanges::mark(c.kind)) + " " + c.designator + "  " + c.text);
        }
        if (!m_built.disagreements.empty()) {
            line("\xE2\x80\x94 The file and the BOM disagree (double-click to take the other) \xE2\x80\x94");
            for (const auto& d : m_built.disagreements)
                line(d.designator + " " + d.field + ": file " + d.cpl + " / BOM " + d.bom + "  \xE2\x86\x92 using the "
                         + (d.takeBom ? "BOM's" : "file's"),
                     d.key());
        }
        if (!m_built.bomOnly.empty()) {
            std::string s;
            for (const std::string& d : m_built.bomOnly) s += " " + d;
            line("In the BOM, not in the file (nowhere to place them):" + s);
        }
        if (!m_built.noBomLine.empty()) {
            std::string s;
            for (const std::string& d : m_built.noBomLine) s += " " + d;
            line("In the file, not in the BOM:" + s);
        }
    }
    m_panel->showReport(lines);

    const bool pendingChange = differs();
    std::string bar;
    if (!pendingChange) bar = j.board.placements.empty() ? "Choose a pick-and-place file to read a board into this job."
                                                         : "The job's board, as read from its sources.";
    else if (!m_ok) bar = "Nothing can be accepted yet.";
    else if (j.board.placements.empty()) bar = "A board read in, not accepted yet.";
    else {
        int added = 0, removed = 0, changed = 0;
        for (const auto& c : changes) {
            if (c.kind == JPBoardChanges::Change::Kind::Added) ++added;
            else if (c.kind == JPBoardChanges::Change::Kind::Removed) ++removed;
            else ++changed;
        }
        bar = "\xE2\x9A\xA0 Changes not accepted yet: " + std::to_string(added) + " added, " + std::to_string(removed)
            + " removed, " + std::to_string(changed) + " changed";
    }
    m_panel->showPending(bar, pendingChange && m_ok, pendingChange);
}

void JPlacerImport::accept() {
    if (!m_ok || !differs()) return;
    JPJob& j = m_job.job();
    // A first board is a new board on the machine; a board read again is the same board.
    const bool first = j.board.placements.empty();
    j.board = JPBoardChanges::merge(j.board, m_built.board);
    j.sources = m_sources;
    j.frame = m_frame;
    j.bomChoices.assign(m_takeBom.begin(), m_takeBom.end());
    const JPPartMatcher::Result r = JPPartMatcher::match(j.board.placements, m_job.library().store(), j.parts);
    m_job.window().showStatus(j.board.name + ": " + std::to_string(j.board.placements.size()) + " placements; "
                                  + std::to_string(r.certain) + " parts matched, " + std::to_string(r.guessed)
                                  + " guessed, " + std::to_string(r.created) + " new",
                              kStatusMs);
    m_job.changed(first ? JPlacerJob::Change::Board : JPlacerJob::Change::Parts);
    fromJob();
    rebuild();
}

} // inline namespace jf
