// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "setup/JPSetupProperties.h"

#include <j/core/JButton.h>
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
//
// Notes and graphs are as wide as the form when it is made: when the form's
// width changes (the window or the divider moved), its pages are made again
// to the new width, the same tab open and each page scrolled as it was.
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

    // A place row's buttons as OpenPnP's LocationButtonsPanel has them (its
    // icons and order: Position Camera, Position Tool, then Get Camera and
    // Get Tool Coordinates), for the forms of OpenPnP's tabs; else Machine
    // Setup's own.
    void setOpenPnpPlaceButtons(bool on) { m_openPnpPlaceButtons = on; }
    // A form of one tab shown without its tab bar (a page inside another tab of that name).
    void setSingleTabBar(bool on) { m_singleTabBar = on; }

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;

    std::function<void(const std::string& property)> onChanged;
    // A button of an Actions row: its action's name.
    std::function<void(const std::string& action)> onAction;
    // A place row's (or an axis row's) take-it-from-the-machine and go-there buttons.
    std::function<void(const JPSetupProperties::Row& row, Tool tool)> onCapture;
    std::function<void(const JPSetupProperties::Row& row, Tool tool)> onMoveTo;

private:
    std::unique_ptr<JWidget> page(const JPSetupProperties::Tab& tab);
    // A group, its labels in a column `labels` wide (the page's widest).
    std::unique_ptr<JWidget> group(const JPSetupProperties::Group& group, float& height, float labels);
    std::unique_ptr<JWidget> editor(const JProperty& p, float width);
    std::unique_ptr<JButton> button(const JPSetupProperties::Cell& c);
    void attachPages(int active);
    void locationButtons(JContainer& row, const JPSetupProperties::Row& r);
    // How wide a control for `p` is, as it is laid out.
    float widthOf(const JProperty& p) const;
    const JProperty* find(const std::string& name) const;
    // The pages made again from the form shown (to a new width).
    void rebuild();
    // Each page made as tall as its groups came out (see page()).
    void fitPages();

    JPSetupProperties::Form                m_form;
    JTabWidget*                            m_tabs = nullptr;
    std::vector<std::unique_ptr<JWidget>>  m_pages;
    std::vector<JContainer*>               m_contents;   // each page's column of groups
    std::vector<std::function<void()>>     m_pulls;
    bool                                   m_pulling = false;   // refresh() setting controls: not an edit
    float                                  m_builtWidth = 0;    // the form's width when its pages were made
    bool                                   m_rebuilding = false;
    bool                                   m_openPnpPlaceButtons = false;
    bool                                   m_singleTabBar = true;
    JContainer*                            m_single = nullptr;   // the one page, without a tab bar
    std::shared_ptr<bool>                  m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
