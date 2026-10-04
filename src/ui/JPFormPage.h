// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPTextField.h"

#include <j/core/JButton.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A page of fields, each a label and its text (committed on Return, Tab or
// leaving it; Escape puts it back) or a choice from a list, a line of notes
// over them, an optional picture beside them, and a row of buttons. A view:
// what is shown is set by its owner, what is typed, chosen or pressed goes
// to the owner.
class JPFormPage : public JContainer {
public:
    struct Field {
        std::string key;
        std::string label;
        bool        choice = false;   // chosen from a list (setChoices), not typed
    };
    struct Action {
        std::string key;
        std::string label;
        bool        newRow = false;   // starts a row of buttons below the last
    };

    JPFormPage(JSceneGraph& graph, const std::vector<Field>& fields, const std::vector<Action>& actions,
               std::unique_ptr<JWidget> picture = nullptr);

    void setNote(const std::string& text);
    void setValue(const std::string& key, const std::string& text);
    void setChoices(const std::string& key, const std::vector<std::string>& items, int chosen);
    // Every field typed into or chosen from, or one of them.
    void setEditable(bool editable);
    void setFieldEditable(const std::string& key, bool editable);
    void setActionEnabled(const std::string& key, bool enabled);
    JWidget* picture() const { return m_picture; }

    std::function<void(const std::string& key, const std::string& text)> onField;
    std::function<void(const std::string& key, int index)>               onChosen;
    std::function<void(const std::string& key)>                          onAction;

private:
    JLabel*                            m_note = nullptr;
    JWidget*                           m_picture = nullptr;
    std::map<std::string, JPTextField*> m_texts;
    std::map<std::string, JComboBox*>  m_choices;
    std::map<std::string, JButton*>    m_buttons;
    bool                               m_updating = false;
};

} // inline namespace jf
