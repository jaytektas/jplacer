// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLibraryHistory.h"

#include "model/JPLibraryJson.h"

#include <j/core/Command.h>

#include <map>
#include <set>

inline namespace jf {

namespace {

// What happened to one kind of thing (parts, packages, footprints): those added, taken away and changed, by uuid,
// each named by `name`.
template <class List, class Name, class Same>
void kindChanges(const List& before, const List& after, const char* kind, const char* kinds, Name name, Same same,
                 std::vector<std::string>& said) {
    std::map<std::string, const typename List::value_type::element_type*> was;
    for (const auto& x : before) was[x->uuid] = x.get();
    std::vector<std::string> added, changed;
    std::set<std::string> kept;
    for (const auto& x : after) {
        const auto it = was.find(x->uuid);
        if (it == was.end()) added.push_back(name(*x));
        else {
            kept.insert(x->uuid);
            if (!same(*it->second, *x)) changed.push_back(name(*x));
        }
    }
    std::vector<std::string> removed;
    for (const auto& x : before)
        if (!kept.count(x->uuid)) removed.push_back(name(*x));
    auto words = [&](const char* verb, const std::vector<std::string>& ids) {
        if (ids.empty()) return;
        said.push_back(ids.size() == 1 ? std::string(verb) + " " + kind + " " + ids.front()
                                       : std::string(verb) + " " + std::to_string(ids.size()) + " " + kinds);
    };
    words("New", added);
    words("Delete", removed);
    words("Change", changed);
}

} // namespace

JPLibraryHistory::JPLibraryHistory(JPConfiguration& config, std::function<void()> restored)
    : m_config(config), m_restored(std::move(restored)) {
    m_stack.setUndoLimit(kSteps);
}

JPLibraryHistory::State JPLibraryHistory::now() const {
    return { m_config.librarySnapshot(), m_config.libraryJson().dump() };
}

void JPLibraryHistory::start() {
    m_stack.clear();
    m_last = std::make_shared<State>(now());
    if (onChanged) onChanged();
}

bool JPLibraryHistory::note() {
    if (!m_last) {
        start();
        return false;
    }
    auto after = std::make_shared<State>(now());
    if (after->text == m_last->text) return false;
    auto before = m_last;
    m_last = after;
    m_recording = true;   // push() does the change; it is done already
    m_stack.push(new JFunctionCommand(
        describe(before->library, after->library),
        [this, after] { if (!m_recording) restore(*after); },
        [this, before] { restore(*before); }, -1));
    m_recording = false;
    ++m_serial;
    if (onChanged) onChanged();
    return true;
}

void JPLibraryHistory::restore(const State& s) {
    m_config.restoreLibrary(s.library);
    m_last = std::make_shared<State>(s);
    if (m_restored) m_restored();
}

void JPLibraryHistory::undo() {
    if (!canUndo()) return;
    m_stack.undo();
    if (onChanged) onChanged();
}

void JPLibraryHistory::redo() {
    if (!canRedo()) return;
    m_stack.redo();
    if (onChanged) onChanged();
}

std::string JPLibraryHistory::describe(const JPLibraryStore::Contents& before, const JPLibraryStore::Contents& after) {
    std::vector<std::string> said;
    kindChanges(before.parts, after.parts, "Part", "Parts", [](const JPPart& p) { return p.id; },
                [](const JPPart& a, const JPPart& b) {
                    return JPLibraryJson::part(a).dump() == JPLibraryJson::part(b).dump();
                }, said);
    kindChanges(before.packages, after.packages, "Package", "Packages", [](const JPPackage& k) { return k.id; },
                [](const JPPackage& a, const JPPackage& b) {
                    return JPLibraryJson::package(a).dump() == JPLibraryJson::package(b).dump();
                }, said);
    kindChanges(before.footprints, after.footprints, "Footprint", "Footprints",
                [](const JPLibraryFootprint& f) { return f.name; },
                [](const JPLibraryFootprint& a, const JPLibraryFootprint& b) {
                    return JPLibraryJson::footprint(a).dump() == JPLibraryJson::footprint(b).dump();
                }, said);
    bool makers = before.manufacturers.size() != after.manufacturers.size();
    for (size_t i = 0; !makers && i < before.manufacturers.size(); ++i)
        makers = before.manufacturers[i].name != after.manufacturers[i].name || before.manufacturers[i].akas != after.manufacturers[i].akas;
    if (makers) said.push_back("Change Manufacturers");
    if (said.empty()) return "Change Library";
    std::string s = said.front();
    for (size_t i = 1; i < said.size(); ++i) s += ", " + said[i];
    return s;
}

} // inline namespace jf
