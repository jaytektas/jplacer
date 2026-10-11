// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerJobFieldsDialog.h"

#include "model/JPSystemUnits.h"
#include "ui/JPLengthCell.h"
#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

// The job's fields, a row each: the field and the job's value in it.
class JPlacerJobFieldsDialog::Rows : public JPTableModel {
public:
    Rows(JPConfiguration& config, JPJob& job, std::function<void()> changed)
        : m_config(config), m_job(job), m_changed(std::move(changed)) {}

    int    columnCount() const override { return 2; }
    Column column(int c) const override {
        return c == 0 ? Column { "Field", "A field added to jobs", Kind::Text }
                      : Column { "Value", "The job's value in it, edited in place", Kind::Text };
    }
    int    rowCount() const override { return int(fields().size()); }
    std::string rowKey(int row) const override { return field(row) ? field(row)->key : std::string(); }
    std::string text(int row, int c) const override {
        const JPField* f = field(row);
        if (!f) return {};
        if (c == 0) return f->name;
        const JJson& v = m_job.custom.get(f->key);
        if (f->type == JPField::Type::Length && v.isNumber()) return JPLengthCell::text(JPLength(v.number(), JPLengthUnit::Millimeters), false);
        if (f->type == JPField::Type::YesNo && v.isBool()) return v.boolean() ? "Yes" : "No";
        return f->text(v);
    }
    bool editable(int, int c) const override { return c == 1; }
    bool setText(int row, int c, const std::string& text, std::string& error) override {
        const JPField* f = field(row);
        if (!f || c != 1) return false;
        JJson v;
        if (!f->parse(text, v, error, JPSystemUnits::units())) return false;
        m_job.custom.set(f->key, v);
        if (m_changed) m_changed();
        return true;
    }

private:
    std::vector<const JPField*> fields() const { return m_config.fields().of(JPFieldSet::Of::Job).added(); }
    const JPField* field(int row) const {
        const auto fs = fields();
        return row >= 0 && size_t(row) < fs.size() ? fs[size_t(row)] : nullptr;
    }

    JPConfiguration&      m_config;
    JPJob&                m_job;
    std::function<void()> m_changed;
};

JPlacerJobFieldsDialog::JPlacerJobFieldsDialog(JPConfiguration& config, JPJob& job, std::function<void()> changed, JGpuHal& hal,
                                               int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Job Fields", kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_version(config.fields().version())
    , m_rows(std::make_unique<Rows>(config, job, std::move(changed))) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_note = m_content->add(std::make_unique<JLabel>(
        g, "The job's own values (an order number, a customer, a run's note), kept in its file. Add Field… makes another; "
           "the import can fill them as a board is imported into the job.",
        0.f));
    m_note->setWordWrap(true);
    auto bar = JPUiParts::row(g);
    JButton* addField = bar->add(JPUiParts::button(g, "Add Field…"));
    addField->setTooltip("A field of your own for jobs");
    addField->onClicked.connect([] {
        if (JPTable::s_fieldHooks.add) JPTable::s_fieldHooks.add(JPFieldSet::Of::Job, nullptr);
    });
    JButton* editFields = bar->add(JPUiParts::button(g, "Edit Fields…"));
    editFields->setTooltip("Rename a field of jobs, change its type or what an import matches it by, or delete it");
    editFields->onClicked.connect([] {
        if (JPTable::s_fieldHooks.manage) JPTable::s_fieldHooks.manage(JPFieldSet::Of::Job);
    });
    m_content->add(std::move(bar));
    m_table = m_content->add(std::make_unique<JPTable>(g));
    m_table->setStateKey("job.fields");
    m_table->setModel(m_rows.get());
    m_table->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    add(m_content.get());
    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Close", JDialogButtonBox::Role::Reject);
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
}

JPlacerJobFieldsDialog::~JPlacerJobFieldsDialog() = default;

void JPlacerJobFieldsDialog::layout(float w, float h) {
    // A field added, edited or deleted meanwhile (its own window): the rows read again.
    if (m_version != m_config.fields().version()) {
        m_version = m_config.fields().version();
        m_table->refresh();
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
