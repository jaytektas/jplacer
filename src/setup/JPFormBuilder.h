// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSetupProperties.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

// Adds settings to a form (JPSetupProperties::Form, shown by JPSetupForm):
// each to the model, and to the layout at the tab and group being filled,
// on a row of its own or on the row begun. Each reads and writes what it
// edits through closures that find it afresh each time (a list it is in can
// move); the "ref" forms take a reference to a member of it. Machine Setup's
// forms are made with it, and the Feeders tab's.
class JPFormBuilder {
public:
    using Strings = std::vector<std::string>;
    // Choices shown by name and kept by id: labels[i] names ids[i].
    struct Named {
        Strings labels, ids;
        void add(std::string label, std::string id) {
            labels.push_back(std::move(label));
            ids.push_back(std::move(id));
        }
    };
    using Form  = JPSetupProperties::Form;
    using Row   = JPSetupProperties::Row;
    using Place = JPSetupProperties::Place;

    explicit JPFormBuilder(Form& f) : m_form(f) {}

    void tab(const std::string& title) {
        m_form.tabs.push_back({ title, {} });
    }
    void group(const std::string& title) {
        if (m_form.tabs.empty()) tab("Configuration");
        m_form.tabs.back().groups.push_back({ title, {} });
    }
    // The settings added until end() go on one row, side by side.
    void row(const std::string& label, Place place = Place::None, const std::string& axis = "") {
        Row r;
        r.label = label;
        r.place = place;
        r.axis = axis;
        rows().push_back(std::move(r));
        m_open = true;
    }
    void end() { m_open = false; }
    // An empty place on the row begun (a column this row has nothing in).
    void skip() { rows().back().cells.push_back({ "", "" }); }
    void header(const Strings& titles) {
        Row r;
        r.kind = Row::Kind::Header;
        for (const std::string& t : titles) r.cells.push_back({ "", t });
        rows().push_back(std::move(r));
    }
    // The rows after this are not in the columns of the header before it.
    void endColumns() { header({}); }
    void note(const std::string& text) {
        Row r;
        r.kind = Row::Kind::Note;
        r.text = text;
        rows().push_back(std::move(r));
    }
    // A graph under the title `label`.
    void plot(const std::string& label, std::shared_ptr<const JPPlot> plot) {
        Row r;
        r.kind = Row::Kind::Plot;
        r.label = label;
        r.plot = std::move(plot);
        rows().push_back(std::move(r));
    }
    // A picture by `label`, `image` read again on each refresh.
    void image(const std::string& label, std::function<std::shared_ptr<const JPFrame>()> image) {
        Row r;
        r.kind = Row::Kind::Image;
        r.label = label;
        r.image = std::move(image);
        rows().push_back(std::move(r));
    }
    // A search's progress across the form, `states` read again on each refresh.
    void strip(std::function<std::vector<int>()> states) {
        Row r;
        r.kind = Row::Kind::Strip;
        r.strip = std::move(states);
        rows().push_back(std::move(r));
    }
    // The place row begun moves and captures with the actuator `name` names
    // (when not empty) in place of the nozzle.
    void actuator(std::function<std::string()> name) { rows().back().actuator = std::move(name); }
    // The place row begun is offsets from `base` (Row::base).
    void base(std::function<std::optional<JPLocation>()> base) { rows().back().base = std::move(base); }
    // An icon button on the row begun (OpenPnP's icon `icon`): the owner does `action`.
    void iconButton(const std::string& action, const std::string& icon, const std::string& tooltip) {
        button(action, action, tooltip);
        rows().back().cells.back().icon = icon;
    }
    // Buttons: (label, action) each; the owner does the action.
    void actions(const std::vector<std::pair<std::string, std::string>>& buttons) {
        Row r;
        r.kind = Row::Kind::Actions;
        for (const auto& [label, action] : buttons) r.cells.push_back({ action, label });
        rows().push_back(std::move(r));
    }

    // `placeholder`: what an empty value stands for, shown greyed.
    void text(const std::string& name, const std::string& label, std::function<std::string()> get,
              std::function<void(const std::string&)> set, const std::string& editor = "",
              const std::string& placeholder = "") {
        JProperty p = make(name, label);
        p.meta.editor = editor;
        p.meta.def = placeholder;
        p.get = [get] { return JVariant(get()); };
        if (set) p.set = [set](const JVariant& v) { set(v.toString()); return true; };   // none: shown, not edited
        put(std::move(p));
    }
    void text(const std::string& name, const std::string& label, std::function<std::string&()> ref,
              const std::string& editor = "") {
        text(name, label, [ref] { return ref(); }, [ref](const std::string& v) { ref() = v; }, editor);
    }
    // A whole number from `min` to `max` on a slider (a pipeline's parameter).
    void slider(const std::string& name, const std::string& label, int min, int max, std::function<int()> get,
                std::function<void(int)> set) {
        JProperty p = make(name, label);
        p.meta.editor = "slider";
        p.meta.min = JVariant(min);
        p.meta.max = JVariant(max);
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(int(v.toInt())); return true; };
        put(std::move(p));
    }
    // A colour, as "#rrggbb", picked from the colour chooser.
    void color(const std::string& name, const std::string& label, std::function<std::string()> get,
               std::function<void(const std::string&)> set) {
        text(name, label, std::move(get), std::move(set), "color");
    }
    void number(const std::string& name, const std::string& label, std::function<double()> get,
                std::function<void(double)> set, int decimals = 3) {
        JProperty p = make(name, label);
        p.meta.decimals = decimals;
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(v.toDouble()); return true; };
        put(std::move(p));
    }
    void number(const std::string& name, const std::string& label, std::function<double&()> ref, int decimals = 3) {
        number(name, label, [ref] { return ref(); }, [ref](double v) { ref() = v; }, decimals);
    }
    void integer(const std::string& name, const std::string& label, std::function<int()> get,
                 std::function<void(int)> set, int min, int max) {
        JProperty p = make(name, label);
        p.meta.min = JVariant(min);
        p.meta.max = JVariant(max);
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(int(v.toInt())); return true; };
        put(std::move(p));
    }
    void integer(const std::string& name, const std::string& label, std::function<int&()> ref, int min, int max) {
        integer(name, label, [ref] { return ref(); }, [ref](int v) { ref() = v; }, min, max);
    }
    void flag(const std::string& name, const std::string& label, std::function<bool()> get, std::function<void(bool)> set) {
        JProperty p = make(name, label);
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(v.toBool()); return true; };
        put(std::move(p));
    }
    void flag(const std::string& name, const std::string& label, std::function<bool&()> ref) {
        flag(name, label, [ref] { return ref(); }, [ref](bool v) { ref() = v; });
    }
    // One of `labels`, read and written as the label.
    void choice(const std::string& name, const std::string& label, const Strings& labels,
                std::function<std::string()> get, std::function<void(const std::string&)> set) {
        JProperty p = make(name, label);
        for (const std::string& l : labels) p.meta.choices.push_back(JVariant(l));
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(v.toString()); return true; };
        put(std::move(p));
    }
    // One of `labels`, or a name typed in (an editable choice).
    void editableChoice(const std::string& name, const std::string& label, const Strings& labels, std::function<std::string()> get,
                        std::function<void(const std::string&)> set) {
        JProperty p = make(name, label);
        p.meta.editor = "editable-choice";
        for (const std::string& l : labels) p.meta.choices.push_back(JVariant(l));
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(v.toString()); return true; };
        put(std::move(p));
    }
    // One of `n`, kept as its id.
    void byName(const std::string& name, const std::string& label, const Named& n, std::function<std::string()> get,
                std::function<void(const std::string&)> set) {
        choice(name, label, n.labels,
               [n, get] {
                   const std::string id = get();
                   for (size_t i = 0; i < n.ids.size(); ++i)
                       if (n.ids[i] == id) return n.labels[i];
                   return id;   // names something not in the cell: shown as it is
               },
               [n, set](const std::string& l) {
                   for (size_t i = 0; i < n.labels.size(); ++i)
                       if (n.labels[i] == l) set(n.ids[i]);
               });
    }
    void byName(const std::string& name, const std::string& label, const Named& n, std::function<std::string&()> ref) {
        byName(name, label, n, [ref] { return ref(); }, [ref](const std::string& id) { ref() = id; });
    }

    // What the last row's label (and the last setting) says when pointed at.
    void tip(const std::string& text) {
        Row& r = rows().back();
        r.tooltip = text;
        if (!r.cells.empty() && !r.cells.back().property.empty())
            if (const JProperty* p = m_form.model.find(r.cells.back().property)) {
                JProperty q = *p;
                q.meta.tooltip = text;
                m_form.model.add(std::move(q));
            }
    }
    // A button on the row begun (or a row of its own): the owner does `action`.
    void button(const std::string& action, const std::string& label, const std::string& tooltip = "", bool enabled = true) {
        JPSetupProperties::Cell c;
        c.property = action;
        c.label = label;
        c.button = true;
        c.enabled = enabled;
        c.tooltip = tooltip;
        if (!m_open) {
            Row r;
            r.kind = Row::Kind::Actions;
            r.cells.push_back(std::move(c));
            rows().push_back(std::move(r));
            return;
        }
        rows().back().cells.push_back(std::move(c));
    }

private:
    std::vector<Row>& rows() {
        if (m_form.tabs.empty() || m_form.tabs.back().groups.empty()) group("");
        return m_form.tabs.back().groups.back().rows;
    }
    JProperty make(const std::string& name, const std::string& label) const {
        JProperty p;
        p.name = name;
        p.meta.category = m_form.tabs.empty() || m_form.tabs.back().groups.empty() ? "" : m_form.tabs.back().groups.back().title;
        p.meta.label = label;
        return p;
    }
    void put(JProperty p) {
        const std::string name = p.name, label = p.meta.label;
        m_form.model.add(std::move(p));
        if (m_open) {
            Row& r = rows().back();
            r.cells.push_back({ name, r.cells.empty() ? "" : label });
            return;
        }
        Row r;
        r.label = label;
        r.cells.push_back({ name, "" });
        rows().push_back(std::move(r));
    }

    Form& m_form;
    bool  m_open = false;
};

} // inline namespace jf
