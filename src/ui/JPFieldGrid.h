// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPGroupFrame.h"
#include "JPTextField.h"

#include <j/core/JCheckBox.h>
#include <j/core/JComboBox.h>
#include <j/core/JContainer.h>
#include <j/core/JLabel.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// Labels and their fields laid out as OpenPnP's settings forms (FormLayout)
// are: so many label-and-field pairs across, filled a row at a time; each
// label against its field, its column as wide as the widest label in it,
// the fields sharing the rest. A field keeps its value until Return, Tab or
// leaving it (JPTextField), as the rest of jplacer's forms.
class JPFieldGrid : public JContainer {
public:
    JPFieldGrid(JSceneGraph& graph, int pairsAcross);

    // A text field; `commit` takes the text, false to put the shown value back.
    JPTextField* text(const std::string& label, const std::string& tooltip, const std::string& value,
                      std::function<bool(const std::string&)> commit);
    JComboBox*   choice(const std::string& label, const std::string& tooltip, const std::vector<std::string>& items,
                        int current, std::function<void(int)> chosen);
    JCheckBox*   tick(const std::string& label, const std::string& tooltip, bool on, std::function<void(bool)> changed);
    // Any widget in the next field's place, under a label (empty: none).
    template <class T>
    T* widget(const std::string& label, std::unique_ptr<T> w) {
        T* raw = w.get();
        place(label, "", std::move(w));
        return raw;
    }
    // An empty place, to start the next pair on a new row or leave a gap.
    void skip();

    // The height it wants: its rows of fields.
    float rowsHeight() const;
    // `grid` in a titled group (OpenPnP's TitledBorder), as tall as its rows.
    static std::unique_ptr<JPGroupFrame> grouped(JSceneGraph& graph, const std::string& title, std::unique_ptr<JPFieldGrid> grid);

private:
    void place(const std::string& label, const std::string& tooltip, std::unique_ptr<JWidget> field);

    int                      m_pairs;
    int                      m_placed = 0;   // pairs placed so far
    std::vector<JContainer*> m_columns;      // a form (labels, fields) for each column of pairs
};

} // inline namespace jf
