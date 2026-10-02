// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/JContainer.h>
#include <j/core/JPropertyModel.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// A property model laid out for editing: each category under its heading, a
// row a property (its label, and a control chosen by its value: a box for a
// yes/no, a number field, a choice, a line of text). A property that cannot
// be written is shown as text. An edit goes straight through the property's
// setter and is reported to onChanged.
class JPPropertyForm : public JContainer {
public:
    explicit JPPropertyForm(JSceneGraph& graph);

    // Show `model` in place of what was shown. The model's closures must stay
    // valid while it is shown.
    void setModel(JPropertyModel model);
    // Read every value again (something else changed them).
    void refresh();

    std::function<void(const std::string& property)> onChanged;

private:
    JPropertyModel                     m_model;
    std::vector<std::function<void()>> m_pulls;
    bool                               m_pulling = false;   // refresh() setting controls: not an edit
};

} // inline namespace jf
