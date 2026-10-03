// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "setup/JPSetupProperties.h"

#include <j/core/JContainer.h>
#include <j/core/JTabWidget.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A part's settings laid out as OpenPnP lays them out (JPSetupProperties::Form):
// a tab each, titled groups down each tab, and in each group a row a
// label: right-aligned labels in a column, controls at the size of what they
// hold (a number as wide as a number, a choice as wide as its longest), the
// columns under a header lined up. A place row ends with buttons to take it
// from where the machine is, or go there; an axis row the same for one axis.
// A setting that cannot be changed is shown as text. An edit goes through
// the property's setter and is reported to onChanged.
class JPSetupForm : public JContainer {
public:
    // What a place row's buttons use.
    enum class Tool { Camera, Nozzle };

    explicit JPSetupForm(JSceneGraph& graph);

    // Show `form` in place of what was shown, on the tab of the same title
    // as before. The model's closures must stay valid while it is shown.
    void setForm(JPSetupProperties::Form form);
    // Read every value again (something else changed them).
    void refresh();
    // Set `property` as an edit would (a place taken from the machine).
    bool set(const std::string& property, const JVariant& value);
    // A property's value now.
    JVariant get(const std::string& property) const;

    std::function<void(const std::string& property)> onChanged;
    // A button of an Actions row: its action's name.
    std::function<void(const std::string& action)> onAction;
    // A place row's (or an axis row's) take-it-from-the-machine and go-there buttons.
    std::function<void(const JPSetupProperties::Row& row, Tool tool)> onCapture;
    std::function<void(const JPSetupProperties::Row& row, Tool tool)> onMoveTo;

private:
    std::unique_ptr<JWidget> page(const JPSetupProperties::Tab& tab);
    std::unique_ptr<JWidget> group(const JPSetupProperties::Group& group, float& height);
    std::unique_ptr<JWidget> editor(const JProperty& p, float width);
    // How wide a control for `p` is, as it is laid out.
    float widthOf(const JProperty& p) const;
    const JProperty* find(const std::string& name) const;

    JPSetupProperties::Form                m_form;
    JTabWidget*                            m_tabs = nullptr;
    std::vector<std::unique_ptr<JWidget>>  m_pages;
    std::vector<std::function<void()>>     m_pulls;
    bool                                   m_pulling = false;   // refresh() setting controls: not an edit
};

} // inline namespace jf
