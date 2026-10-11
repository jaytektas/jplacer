// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <string>

inline namespace jf {

// Every table's windows for its fields (DESIGN.md, Fields): JPTable's hooks (s_fields, s_fieldHooks) set to
// the configuration's fields and to Add a Field, Edit a Field and Fields of … (JPlacerFieldDialog,
// JPlacerFieldsDialog). A change kept: the configuration saved (fields.json with it), every table shown
// anew, and `changed` told (the library's undo noted, the tabs shown again).
class JPlacerFieldWindows {
public:
    JPlacerFieldWindows(JAppWindow& window, JPlacerJob& job, std::function<void()> changed);
    ~JPlacerFieldWindows();

    // A field added to `of`, named `name` to begin with (and matched by it as an import's heading).
    void add(JPFieldSet::Of of, const std::string& name, std::function<void(const std::string& key)> added);
    void edit(JPFieldSet::Of of, const std::string& key);
    void manage(JPFieldSet::Of of);

private:
    void kept();

    JAppWindow&           m_window;
    JPlacerJob&           m_job;
    std::function<void()> m_changed;
};

} // inline namespace jf
