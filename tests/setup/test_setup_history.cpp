// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Machine Setup's undo and redo: steps back and forward, a run of changes to
// one thing as one step, and a new change dropping what could be redone.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPSetupHistory.h"

#include <cstdio>

using namespace jf;

namespace {

JPSetupHistory::State named(const std::string& name, const std::string& selected = "machine") {
    JPSetupHistory::State s;
    s.cell.name = name;
    s.selected = selected;
    return s;
}

} // namespace

int main() {
    JPSetupHistory::State now = named("A");
    int restores = 0, changes = 0;
    JPSetupHistory history([&](const JPSetupHistory::State& s) { now = s; ++restores; });
    history.onChanged = [&] { ++changes; };
    assert(!history.canUndo() && !history.canRedo());

    // Recording does not restore: the change is made already.
    auto change = [&](const std::string& what, const std::string& key, const std::string& to) {
        JPSetupHistory::State before = now;
        now.cell.name = to;
        history.record(what, key, before, now);
    };
    change("Name", "machine|name", "AB");
    change("Name", "machine|name", "ABC");   // typing on: the same step
    assert(restores == 0 && changes == 2 && history.canUndo() && history.undoText() == "Name");
    history.undo();
    assert(now.cell.name == "A" && !history.canUndo() && history.canRedo());
    history.redo();
    assert(now.cell.name == "ABC");

    // Another thing changed is a step of its own; so is the same thing after seal().
    change("Add Camera", "", "ABCD");
    history.seal();
    change("Name", "machine|name", "ABCDE");
    history.undo();
    assert(now.cell.name == "ABCD");
    history.undo();
    assert(now.cell.name == "ABC");

    // A change after undoing drops what could have been redone.
    change("Name", "machine|name", "X");
    assert(!history.canRedo());
    history.undo();
    assert(now.cell.name == "ABC");

    // The node selected goes back with it.
    JPSetupHistory::State before = now;
    now = named("ABC", "camera:C1");
    history.record("Remove Top", "", before, now);
    history.undo();
    assert(now.selected == before.selected);

    std::puts("test_setup_history: ok");
    return 0;
}
