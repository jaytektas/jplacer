// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerUndo.h"

#include <algorithm>

inline namespace jf {

int JPlacerUndo::add(Source s) {
    m_seen.push_back(s.serial ? s.serial() : 0);
    m_sources.push_back(std::move(s));
    update();
    return int(m_sources.size()) - 1;
}

void JPlacerUndo::changed(int id) {
    if (id < 0 || size_t(id) >= m_sources.size()) return;
    const long long serial = m_sources[size_t(id)].serial();
    if (!m_acting) {
        if (serial < m_seen[size_t(id)]) {
            // Started again (another machine opened): its steps are gone.
            std::erase(m_done, id);
            std::erase(m_undone, id);
        } else if (serial > m_seen[size_t(id)]) {
            // A new change: last in line, and what was undone can no longer be done again.
            for (long long i = m_seen[size_t(id)]; i < serial; ++i) m_done.push_back(id);
            m_undone.clear();
        }
    }
    m_seen[size_t(id)] = serial;
    update();
}

void JPlacerUndo::undo() {
    // The last change's history; one with nothing left to undo (steps let go past its limit) is passed over.
    while (!m_done.empty()) {
        const int id = m_done.back();
        m_done.pop_back();
        Source& s = m_sources[size_t(id)];
        if (!s.canUndo()) continue;
        m_acting = true;
        s.undo();
        m_acting = false;
        m_undone.push_back(id);
        break;
    }
    update();
}

void JPlacerUndo::redo() {
    while (!m_undone.empty()) {
        const int id = m_undone.back();
        m_undone.pop_back();
        Source& s = m_sources[size_t(id)];
        if (!s.canRedo()) continue;
        m_acting = true;
        s.redo();
        m_acting = false;
        m_done.push_back(id);
        break;
    }
    update();
}

void JPlacerUndo::setItems(JMenuItem* undo, JMenuItem* redo) {
    m_undoItem = undo;
    m_redoItem = redo;
    update();
}

void JPlacerUndo::update() {
    const Source* u = nullptr;
    for (auto it = m_done.rbegin(); it != m_done.rend() && !u; ++it)
        if (m_sources[size_t(*it)].canUndo()) u = &m_sources[size_t(*it)];
    const Source* r = nullptr;
    for (auto it = m_undone.rbegin(); it != m_undone.rend() && !r; ++it)
        if (m_sources[size_t(*it)].canRedo()) r = &m_sources[size_t(*it)];
    if (m_undoItem) {
        m_undoItem->setEnabled(u != nullptr);
        m_undoItem->setLabel(u ? "Undo " + u->undoText() : "Undo");
    }
    if (m_redoItem) {
        m_redoItem->setEnabled(r != nullptr);
        m_redoItem->setLabel(r ? "Redo " + r->redoText() : "Redo");
    }
}

} // inline namespace jf
