// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerFieldDialog.h"

#include "model/JPFieldRecords.h"
#include "ui/JPFieldGrid.h"

#include <j/core/Dialog.h>
#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {

// A value at most this many named when a type cannot read them all.
constexpr size_t kNamedValues = 5;

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && uint8_t(s[b]) <= ' ') ++b;
    while (e > b && uint8_t(s[e - 1]) <= ' ') --e;
    return s.substr(b, e - b);
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s + ",") {
        if (c == ',') {
            if (!trim(cur).empty()) out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

} // namespace

JPlacerFieldDialog::JPlacerFieldDialog(JPConfiguration& config, JPJob* job, JPFieldSet::Of of, std::string key,
                                       const std::string& suggested, std::function<void(const std::string&, bool)> done,
                                       JGpuHal& hal, int sx, int sy,
                                       NativeWinHandleType parent)
    : JDialogWindow(key.empty() ? std::string("Add a Field to ") + JPFieldSet::plural(of) : std::string("Edit a Field of ") + JPFieldSet::plural(of),
                    kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_job(job)
    , m_of(of)
    , m_key(std::move(key))
    , m_done(std::move(done)) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    const JPFieldSet& set = m_config.fields().of(m_of);
    const JPField* f = m_key.empty() ? nullptr : set.field(m_key);
    const std::string kind = lower(JPFieldSet::plural(m_of));
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    std::string about;
    if (!f) about = "A field of your own for " + kind + ": a column in every table of " + kind + " (shown where you added it, "
                    "chosen in the others with Columns…), searched like the rest, and offered by the import to fill.";
    else if (f->builtIn)
        about = f->name + " is jplacer's own" + (f->about.empty() ? "" : " (" + f->about + ")") + ": rename it" +
                (f->imported() ? ", or say which other headings an import matches it by." : ".") +
                (f->required ? " jplacer needs it, so it stays." : "");
    else about = f->name + ", a field of your own for " + kind + ".";
    m_about = m_content->add(std::make_unique<JLabel>(g, about, 0.f));
    m_about->setWordWrap(true);

    auto grid = std::make_unique<JPFieldGrid>(g, 1);
    auto line = [&](const std::string& label, const std::string& tip, const std::string& value) {
        auto e = std::make_unique<JLineEdit>(g, "");
        e->setText(value);
        e->setTooltip(tip);
        JLineEdit* raw = grid->widget(label, std::move(e));
        raw->onTextChanged.connect([this](const std::string&) { check(); });
        return raw;
    };
    m_name = line("Name", "What its column's heading says, unique among the fields of " + kind, f ? f->name : suggested);
    std::vector<std::string> types;
    for (const JPField::Type t : JPField::types()) types.push_back(JPField::typeLabel(t));
    m_type = grid->widget("Type", std::make_unique<JComboBox>(g, types));
    m_type->setCurrentIndex(f ? int(f->type) : 0);
    m_type->setTooltip(f && f->builtIn ? "A built-in's values are jplacer's own, of its own kind"
                                       : "Text as typed; a Number; a Length (in any units, shown in the System Units); "
                                         "Yes/No, a tick; a Choice from its list");
    m_type->setEnabled(!f || !f->builtIn);
    m_type->onIndexChanged.connect([this](int) { check(); });
    m_choices = line("Choices", "A Choice's choices, commas between (Reel, Cut tape, Tray)", f ? joined(f->choices) : std::string());
    m_matches = line("Import matches", "The headings of a CAD file's or BOM's column the import takes for it, commas between "
                                       "(its name always counts); upper and lower case, spaces and punctuation aside",
                     f ? joined(f->matches) : suggested);
    m_matches->setEnabled(!f || f->imported());
    m_content->add(std::move(grid));
    m_note = m_content->add(std::make_unique<JLabel>(g, "", 0.f));
    m_note->setWordWrap(true);
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    if (f && !f->required) {
        JButton* del = m_buttons->addButton(f->builtIn ? "Remove…" : "Delete…", JDialogButtonBox::Role::Destructive);
        del->setTooltip(f->builtIn ? "Taken out of every table, the import and the search (Edit Fields… restores it); its "
                                     "values are jplacer's and stay"
                                   : "Gone, and its values in every one of the " + kind + " with it (how many is said first)");
        del->onClicked.connect([this] { remove(); });
    }
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject)->setTooltip("Keep nothing");
    m_ok = m_buttons->addButton(f ? "OK" : "Add", JDialogButtonBox::Role::Accept);
    m_buttons->onReject.connect([this] { close(); });
    m_buttons->onAccept.connect([this] { keep(); });
    add(m_buttons.get());
    check();
}

bool JPlacerFieldDialog::typed(JPField& f, std::string& why) const {
    const JPFieldSet& set = m_config.fields().of(m_of);
    if (const JPField* now = m_key.empty() ? nullptr : set.field(m_key)) f = *now;
    f.name = trim(m_name->text());
    if (!f.builtIn) {
        f.type = JPField::types()[size_t(std::clamp(m_type->currentIndex(), 0, int(JPField::types().size()) - 1))];
        f.choices = f.type == JPField::Type::Choice ? split(m_choices->text()) : std::vector<std::string>();
    }
    f.matches = split(m_matches->text());
    if (!set.nameFree(f.name, m_key, why)) return false;
    if (f.type == JPField::Type::Choice && f.choices.empty()) {
        why = "A Choice needs its choices, commas between.";
        return false;
    }
    return true;
}

void JPlacerFieldDialog::check() {
    JPField f;
    std::string why;
    const bool ok = typed(f, why);
    m_choices->setEnabled(!f.builtIn && f.type == JPField::Type::Choice);
    m_ok->setEnabled(ok);
    m_ok->setTooltip(ok ? (m_key.empty() ? "Add the field" : "Keep the field as it is now") : why);
    m_note->setText(ok ? std::string() : why);
}

void JPlacerFieldDialog::keep() {
    JPField f;
    std::string why;
    if (!typed(f, why)) return;
    JPFieldSet& set = m_config.fields().of(m_of);
    if (m_key.empty()) {
        finish(set.add(f), false);
        return;
    }
    const JPField before = *set.field(m_key);
    auto update = [this, f, before] {
        JPFieldSet& s = m_config.fields().of(m_of);
        std::string error;
        if (!s.update(f, error)) {
            m_note->setText(error);
            return;
        }
        if (!before.builtIn && (before.type != f.type || before.choices != f.choices))
            JPFieldRecords::convert(m_config, m_job, m_of, before, f);
        finish(f.key, false);
    };
    // Its values read as its new type: those it cannot read named first, and emptied once agreed.
    const std::vector<std::string> lost = before.builtIn ? std::vector<std::string>()
                                                         : JPFieldRecords::unconvertible(m_config, m_job, m_of, before, f);
    if (lost.empty()) {
        update();
        return;
    }
    std::string named;
    for (size_t i = 0; i < lost.size() && i < kNamedValues; ++i) named += (named.empty() ? "" : ", ") + lost[i];
    if (lost.size() > kNamedValues) named += " and " + std::to_string(lost.size() - kNamedValues) + " more";
    JDialog::confirm("Change " + before.name + "'s Type",
                     std::to_string(lost.size()) + " of " + before.name + "'s values cannot be read as " +
                         (f.type == JPField::Type::Choice ? std::string("one of its choices") : std::string("a ") + lower(JPField::typeLabel(f.type))) +
                         " (" + named + "): they are emptied. Change it?",
                     update);
}

void JPlacerFieldDialog::remove() {
    const JPField* f = m_config.fields().of(m_of).field(m_key);
    if (!f || f->required) return;
    const std::string key = m_key, name = f->name;
    if (f->builtIn) {
        m_config.fields().of(m_of).remove(key);
        finish(key, true);
        return;
    }
    const int n = JPFieldRecords::count(m_config, m_job, m_of, key);
    auto gone = [this, key] {
        JPFieldRecords::erase(m_config, m_job, m_of, key);
        m_config.fields().of(m_of).remove(key);
        finish(key, true);
    };
    if (n == 0) {
        gone();
        return;
    }
    JDialog::confirm("Delete " + name,
                     std::to_string(n) + " of the " + lower(JPFieldSet::plural(m_of)) + " have a value in " + name +
                         ": they are deleted with it. Delete it?",
                     gone);
}

void JPlacerFieldDialog::finish(const std::string& key, bool removed) {
    m_config.fields().changed();
    close();
    if (m_done) m_done(key, removed);
}

void JPlacerFieldDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_about->setMinimumSize(0.f, std::max(st.labelHeight, m_about->heightFor(cw)));
    m_note->setMinimumSize(0.f, std::max(st.labelHeight, m_note->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, 0.f, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
