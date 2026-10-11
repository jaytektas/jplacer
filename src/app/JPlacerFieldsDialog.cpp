// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerFieldsDialog.h"

#include "model/JPFieldRecords.h"
#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

} // namespace

// The fields, a row each, in their order.
class JPlacerFieldsDialog::Rows : public JPTableModel {
public:
    enum Col { kName, kType, kWhose, kNeeded, kImport, kValues, kColumns };

    Rows(JPConfiguration& config, JPJob* job, JPFieldSet::Of of) : m_config(config), m_job(job), m_of(of) { reload(); }

    // The fields read again, with how many have a value in each of yours.
    void reload() {
        m_counts.clear();
        for (const JPField& f : set().fields())
            m_counts.push_back(f.builtIn ? -1 : JPFieldRecords::count(m_config, m_job, m_of, f.key));
    }
    const JPField* field(int row) const {
        const auto& fs = set().fields();
        return row >= 0 && size_t(row) < fs.size() ? &fs[size_t(row)] : nullptr;
    }

    int    columnCount() const override { return kColumns; }
    Column column(int c) const override {
        switch (c) {
            case kName:   return { "Name", "Its column's heading", Kind::Text };
            case kType:   return { "Type", "What its values are (a built-in's: jplacer's own)", Kind::Text };
            case kWhose:  return { "Whose", "jplacer's own, or one you added", Kind::Text };
            case kNeeded: return { "Needed", "jplacer cannot work without it: it stays", Kind::Text };
            case kImport: return { "Import Matches", "The headings an import's column is taken for it by (its name too)", Kind::Text };
            case kValues: return { "Values", "How many have a value in it (a field of yours)", Kind::Number };
        }
        return {};
    }
    int    rowCount() const override { return int(set().fields().size()); }
    std::string rowKey(int row) const override { return field(row) ? field(row)->key : std::string(); }
    std::string text(int row, int c) const override {
        const JPField* f = field(row);
        if (!f) return {};
        switch (c) {
            case kName:   return f->removed ? f->name + " (removed)" : f->name;
            case kType:   return f->builtIn ? "jplacer's" : JPField::typeLabel(f->type);
            case kWhose:  return f->builtIn ? "jplacer" : "Yours";
            case kNeeded: return f->required ? "Yes" : "";
            case kImport: {
                if (!f->imported()) return "";
                std::string s;
                for (const std::string& m : f->matches) s += (s.empty() ? "" : ", ") + m;
                return s.empty() ? "(its name)" : s;
            }
            case kValues: return f->builtIn ? std::string() : std::to_string(number(row, c));
        }
        return {};
    }
    double number(int row, int) const override {
        return row >= 0 && size_t(row) < m_counts.size() && m_counts[size_t(row)] > 0 ? m_counts[size_t(row)] : 0;
    }
    bool cellDimmed(int row, int) const override { return field(row) && field(row)->removed; }

private:
    const JPFieldSet& set() const { return m_config.fields().of(m_of); }

    JPConfiguration& m_config;
    JPJob*           m_job;
    JPFieldSet::Of   m_of;
    std::vector<int> m_counts;
};

JPlacerFieldsDialog::JPlacerFieldsDialog(JPConfiguration& config, JPJob* job, JPFieldSet::Of of,
                                         std::function<void(const std::string&)> openField, std::function<void()> restored,
                                         JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow(std::string("Fields of ") + JPFieldSet::plural(of), kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_of(of)
    , m_openField(std::move(openField))
    , m_restored(std::move(restored))
    , m_version(config.fields().version())
    , m_rows(std::make_unique<Rows>(config, job, of)) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    const std::string kind = lower(JPFieldSet::plural(of));
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_note = m_content->add(std::make_unique<JLabel>(
        g, "Every field of " + kind + ": jplacer's own and yours. Every table of " + kind + " has a column for each "
           "(Columns… shows or hides it), the search looks in them all, and the import fills those it matches. Rename "
           "any of them; delete one of yours, or remove one of jplacer's you have no use for (one it needs stays).",
        0.f));
    m_note->setWordWrap(true);
    auto bar = JPUiParts::row(g);
    JButton* addField = bar->add(JPUiParts::button(g, "Add Field…"));
    addField->setTooltip("A field of your own for " + kind);
    addField->onClicked.connect([this] {
        if (m_openField) m_openField(std::string());
    });
    m_edit = bar->add(JPUiParts::button(g, "Edit…"));
    m_edit->setTooltip("Rename the field chosen, change its type or what an import matches it by, or delete it");
    m_edit->onClicked.connect([this] {
        if (const JPField* f = chosen(); f && !f->removed && m_openField) m_openField(f->key);
    });
    m_restore = bar->add(JPUiParts::button(g, "Restore"));
    m_restore->setTooltip("The field of jplacer's chosen, removed, back in every table, the import and the search");
    m_restore->onClicked.connect([this] {
        const JPField* f = chosen();
        if (!f || !f->removed) return;
        m_config.fields().of(m_of).restore(f->key);
        m_config.fields().changed();
        if (m_restored) m_restored();
    });
    m_content->add(std::move(bar));
    m_table = m_content->add(std::make_unique<JPTable>(g));
    m_table->setStateKey("fields");
    m_table->setModel(m_rows.get());
    m_table->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_table->onSelectionChanged.connect([this] { update(); });
    m_table->onRowActivated.connect([this](int) {
        if (const JPField* f = chosen(); f && !f->removed && m_openField) m_openField(f->key);
    });
    add(m_content.get());
    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Close", JDialogButtonBox::Role::Reject);
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
    update();
}

JPlacerFieldsDialog::~JPlacerFieldsDialog() = default;

const JPField* JPlacerFieldsDialog::chosen() const { return m_rows->field(m_table->selectedRow()); }

void JPlacerFieldsDialog::update() {
    const JPField* f = chosen();
    m_edit->setEnabled(f && !f->removed);
    m_restore->setEnabled(f && f->removed);
}

void JPlacerFieldsDialog::layout(float w, float h) {
    // A field added, edited or restored meanwhile (its own window): the rows read again.
    if (m_version != m_config.fields().version()) {
        m_version = m_config.fields().version();
        m_rows->reload();
        m_table->refresh();
        update();
    }
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_note->setMinimumSize(0.f, std::max(st.labelHeight, m_note->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
