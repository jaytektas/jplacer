// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementsHoldersGroup.h"

#include "JPUiParts.h"

#include <j/core/Dialog.h>
#include <j/core/JSeparator.h>

#include <cctype>
#include <filesystem>

inline namespace jf {

// OpenPnP's words for boards and for panels.
struct JPPlacementsHoldersGroup::Words {
    const char* title;
    const char* suffix;
    const char* add;
    const char* addTip;
    const char* createNew;
    const char* createNewDialog;
    const char* createNewError;
    const char* existing;
    const char* existingError;
    const char* remove;
    const char* removeTip;
    const char* removeErrorTitle;
    const char* removeErrorTail;
    const char* copy;
    const char* copyTip;
    const char* copyDialog;
    const char* copyError;
    const char* cleanUpTip;
};

namespace {

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}

// A name chosen to save as, given the suffix (".jpboard") when it lacks it.
std::string withSuffix(std::string path, const std::string& suffix) {
    std::string lower = path;
    for (char& c : lower) c = char(std::tolower(uint8_t(c)));
    if (lower.size() < suffix.size() || lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) != 0)
        path += suffix;
    return path;
}

} // namespace

// What files of this kind are saved as (a board: jplacer's), and what may be opened (a board: OpenPnP's too).
std::vector<std::string> JPPlacementsHoldersGroup::saveExtensions() const {
    return m_kind == JPPlacementsHolder::Kind::Board ? std::vector<std::string>{ "jpboard" } : std::vector<std::string>{ "xml" };
}

std::vector<std::string> JPPlacementsHoldersGroup::openExtensions() const {
    return m_kind == JPPlacementsHolder::Kind::Board ? std::vector<std::string>{ "jpboard", "xml" } : std::vector<std::string>{ "xml" };
}

const JPPlacementsHoldersGroup::Words& JPPlacementsHoldersGroup::words() const {
    static const Words board {
        "Boards", JPBoard::kExtension, "Add Board...", "Add a new or existing board", "Create New Board...",
        "Save New Board As...", "Unable to create new board", "Existing Board", "Board load failed", "Remove Board",
        "Remove the selected board(s)", "Error Removing Board",
        " because it is either being used by the current job or by a panel that is loaded in the current configuration.",
        "Copy Board...", "Copy the selected board", "Save Copy of Board As...", "Unable to create copy of board",
        "Remove any boards that are not in use by the currently loaded job or by any currently loaded panel. Does not "
        "delete them from the file system."
    };
    static const Words panel {
        "Panels", ".panel.xml", "Add Panel...", "Add a new or existing panel", "Create New Panel...",
        "Save New Panel As...", "Unable to create new panel", "Existing Panel", "Panel load failed", "Remove Panel",
        "Remove the selected panels(s)", "Error Removing Panel",
        " because it is either being used by the current job or is a subpanel of another panel that is loaded in the "
        "current configuration.",
        "Copy Panel...", "Copy the selected panel", "Save Copy of Panel As...", "Unable to create copy of panel",
        "Remove any panels that are not in use by the currently loaded job. Does not delete them from the file system."
    };
    return m_kind == JPPlacementsHolder::Kind::Board ? board : panel;
}

JPPlacementsHoldersGroup::JPPlacementsHoldersGroup(JSceneGraph& graph, JPConfiguration& config,
                                                   JPPlacementsHolder::Kind kind, std::function<const JPJob*()> job)
    : JPGroupFrame(graph, kind == JPPlacementsHolder::Kind::Board ? "Boards" : "Panels")
    , m_config(config)
    , m_kind(kind)
    , m_job(job)
    , m_model(config, kind, job) {
    setAlignItems(JAlignItems::Stretch);
    setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_model.onChanged = [this] { changed(); };
    const Words& w = words();

    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    m_add = tool(w.add, "general-add", w.addTip);
    m_add->setLeads(JPIconButton::Leads::Menu);
    m_add->onClicked.connect([this] { showAddMenu(); });
    m_remove = tool(w.remove, "general-remove", w.removeTip);
    m_remove->onClicked.connect([this] {
        std::vector<std::string> files;
        for (const JPPlacementsHolder* h : selections()) files.push_back(h->file);
        remove(files, true, false, false);
    });
    m_copy = tool(w.copy, "copy", w.copyTip);
    m_copy->setLeads(JPIconButton::Leads::Elsewhere);
    m_copy->onClicked.connect([this] { copy(); });
    bar->add(toolSeparator(graph));
    tool("Clean Up", "clean-sweep", w.cleanUpTip)->onClicked.connect([this] {
        std::vector<std::string> unused;
        for (const auto& h : known())
            if (!m_config.isInUse(*h, m_job())) unused.push_back(h->file);
        // Panels in panels: OpenPnP cleans up again until nothing more goes.
        remove(unused, false, m_kind == JPPlacementsHolder::Kind::Panel, false);
    });
    add(std::move(bar));
    m_table = add(std::make_unique<JPTable>(graph));
    m_table->setModel(&m_model);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] { selectionChanged(); });
    m_table->onEditRefused = [](const std::string&) {};
    selectionChanged();
}

const std::vector<std::shared_ptr<JPPlacementsHolder>> JPPlacementsHoldersGroup::known() const {
    std::vector<std::shared_ptr<JPPlacementsHolder>> out;
    if (m_kind == JPPlacementsHolder::Kind::Board)
        for (const auto& b : m_config.boards()) out.push_back(b);
    else
        for (const auto& p : m_config.panels()) out.push_back(p);
    return out;
}

void JPPlacementsHoldersGroup::refresh() {
    m_table->refresh();
    selectionChanged();
}

std::vector<JPPlacementsHolder*> JPPlacementsHoldersGroup::selections() const {
    std::vector<JPPlacementsHolder*> out;
    for (const int r : m_table->selectedRows())
        if (JPPlacementsHolder* h = m_model.holder(r)) out.push_back(h);
    return out;
}

void JPPlacementsHoldersGroup::select(const JPPlacementsHolder* h) {
    if (!h) {
        m_table->clearSelection();
        return;
    }
    if (const int r = m_model.rowOf(h); r >= 0) m_table->selectRow(r);
}

void JPPlacementsHoldersGroup::selectionChanged() {
    const auto s = selections();
    // As OpenPnP's action groups: Remove for one or more, Copy for one.
    m_remove->setEnabled(!s.empty());
    m_copy->setEnabled(s.size() == 1);
    JPPlacementsHolder* shown = s.size() == 1 ? s.front() : nullptr;
    if (shown != m_shown) {
        m_shown = shown;
        if (onShown) onShown(shown);
    }
}

void JPPlacementsHoldersGroup::changed() {
    if (onChanged) onChanged();
}

void JPPlacementsHoldersGroup::showAddMenu() {
    if (!openMenu) return;
    const Words& w = words();
    m_addMenu = std::make_unique<JMenu>(w.add);
    m_addMenu->add(m_graph, w.createNew)->onTriggered.connect([this] {
        const Words& w2 = words();
        JDialog::saveFile(w2.createNewDialog, saveExtensions(), [this](std::string path) {
            addFile(withSuffix(path, words().suffix), words().createNewError);
        });
    });
    m_addMenu->add(m_graph, w.existing)->onTriggered.connect([this] {
        JDialog::openFile(words().existing, openExtensions(), [this](std::string path) { addFile(path, words().existingError); });
    });
    const JRect b = m_graph.getLayoutConst(m_add->getNodeId()).boundingBox;
    openMenu(m_addMenu.get(), b.x + b.width, b.y + b.height);
}

void JPPlacementsHoldersGroup::addFile(const std::string& path, const char* errorTitle) {
    std::string error;
    std::shared_ptr<JPPlacementsHolder> h;
    if (m_kind == JPPlacementsHolder::Kind::Board) h = m_config.board(path, error);
    else h = m_config.panel(path, error);
    if (!h) {
        JDialog::message(errorTitle, error);
        return;
    }
    m_table->refresh();
    select(h.get());
    changed();
}

void JPPlacementsHoldersGroup::remove(std::vector<std::string> files, bool reportInUse, bool repeat, bool removedAny) {
    if (files.empty()) {
        // Panels nested in panels: another pass while the last took any away.
        if (repeat && removedAny) {
            std::vector<std::string> unused;
            for (const auto& h : known())
                if (!m_config.isInUse(*h, m_job())) unused.push_back(h->file);
            if (!unused.empty()) {
                remove(unused, reportInUse, repeat, false);
                return;
            }
        }
        m_table->refresh();
        selectionChanged();
        changed();
        return;
    }
    const std::string file = files.front();
    files.erase(files.begin());
    std::shared_ptr<JPPlacementsHolder> h;
    for (const auto& k : known())
        if (k->file == file) h = k;
    if (!h) {
        remove(files, reportInUse, repeat, removedAny);
        return;
    }
    auto next = [this, files, reportInUse, repeat](bool removed) { remove(files, reportInUse, repeat, removed); };
    if (m_config.isInUse(*h, m_job())) {
        if (reportInUse) {
            JDialog::message(words().removeErrorTitle,
                             "Could not remove " + h->name.value_or("") + words().removeErrorTail,
                             [next, removedAny] { next(removedAny); });
            return;
        }
        next(removedAny);
        return;
    }
    auto drop = [this, file, next] {
        for (const auto& k : known())
            if (k->file == file) {
                if (m_shown == k.get()) {
                    m_shown = nullptr;
                    if (onShown) onShown(nullptr);
                }
                if (m_kind == JPPlacementsHolder::Kind::Board) m_config.removeBoard(static_cast<const JPBoard*>(k.get()));
                else m_config.removePanel(static_cast<const JPPanel*>(k.get()));
                break;
            }
        m_table->refresh();
        next(true);
    };
    if (h->dirty && confirmSave) confirmSave(*h, drop);
    else drop();
}

void JPPlacementsHoldersGroup::copy() {
    const auto s = selections();
    if (s.size() != 1) return;
    const std::string sourceFile = s.front()->file;
    JDialog::saveFile(words().copyDialog, saveExtensions(), [this, sourceFile](std::string chosen) {
        std::shared_ptr<JPPlacementsHolder> from;
        for (const auto& k : known())
            if (k->file == sourceFile) from = k;
        if (!from) return;
        const std::string path = JPConfiguration::canonical(withSuffix(chosen, words().suffix));
        std::shared_ptr<JPPlacementsHolder> copy;
        std::string error;
        bool saved;
        if (m_kind == JPPlacementsHolder::Kind::Board) {
            auto b = std::make_shared<JPBoard>(*static_cast<const JPBoard*>(from.get()));
            b->makeDefinition();
            b->ownParts();   // its parts its own, not the board's it was copied from
            b->file = path;
            b->name = std::filesystem::path(path).filename().string();
            b->dirty = false;
            saved = m_config.saveBoard(*b, error);
            if (saved) m_config.addBoard(b);
            copy = b;
        } else {
            auto p = std::make_shared<JPPanel>(*static_cast<const JPPanel*>(from.get()));
            p->makeDefinition();
            // Its children its own, not uses of the panel copied.
            for (auto& c : p->children) c->makeDefinition();
            p->file = path;
            p->name = std::filesystem::path(path).filename().string();
            p->dirty = false;
            saved = m_config.savePanel(*p, error);
            if (saved) m_config.addPanel(p);
            copy = p;
        }
        if (!saved) {
            JDialog::message(words().copyError, error);
            return;
        }
        m_table->refresh();
        select(copy.get());
        changed();
    });
}

} // inline namespace jf
