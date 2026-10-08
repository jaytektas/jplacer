// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerManufacturersDialog.h"

#include "ui/JPIconButton.h"
#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

namespace {

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

} // namespace

JPlacerManufacturersDialog::JPlacerManufacturersDialog(JPConfiguration& config, std::function<void()> changed, JGpuHal& hal,
                                                       int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Manufacturers' Names", kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_changed(std::move(changed)) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_note = m_content->add(std::make_unique<JLabel>(g, "Each manufacturer and the other names files give it, commas "
                                                        "between (Texas Instruments: TI, Texas Instruments Inc.). A BOM's "
                                                        "manufacturer by any of them is that manufacturer.", 0.f));
    m_note->setWordWrap(true);
    m_rows = m_content->add(std::make_unique<JScrollArea>(g, 0.f, 0.f));
    m_rows->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    JButton* addButton = m_content->add(JPUiParts::button(g, "Add a Manufacturer"));
    addButton->setTooltip("A new row, for a manufacturer and its other names");
    addButton->onClicked.connect([this] {
        m_config.manufacturers().push_back({ "", {} });
        m_refill = true;
    });
    add(m_content.get());
    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Close", JDialogButtonBox::Role::Reject);
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
}

JPlacerManufacturersDialog::~JPlacerManufacturersDialog() {
    // A row left without a name names no one.
    auto& list = m_config.manufacturers();
    list.erase(std::remove_if(list.begin(), list.end(), [](const JPManufacturer& m) { return trim(m.name).empty(); }), list.end());
    if (m_changed) m_changed();
}

void JPlacerManufacturersDialog::fill() {
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_rows->clearChildren();
    auto& list = m_config.manufacturers();
    const float nameW = JTextHelper::measureWidth("Samsung Electro-Mechanics") + 2 * st.fieldPadding;
    for (size_t i = 0; i < list.size(); ++i) {
        auto row = JPUiParts::row(g);
        row->setVSizePolicy(JSizePolicyMode::Fixed);
        row->setSize(0.f, st.controlHeight);
        JPIconButton* remove = row->add(std::make_unique<JPIconButton>(g, "Delete", "general-remove", "Delete this manufacturer"));
        remove->onClicked.connect([this, i] {
            auto& l = m_config.manufacturers();
            if (i < l.size()) l.erase(l.begin() + std::ptrdiff_t(i));
            m_refill = true;
        });
        JLineEdit* name = row->add(std::make_unique<JLineEdit>(g, "Name", nameW));
        name->setHSizePolicy(JSizePolicyMode::Fixed);
        name->setText(list[i].name);
        name->onTextChanged.connect([this, i](const std::string& t) {
            if (i < m_config.manufacturers().size()) m_config.manufacturers()[i].name = trim(t);
        });
        JLineEdit* others = row->add(std::make_unique<JLineEdit>(g, "Other names, commas between"));
        others->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        others->setText(joined(list[i].akas));
        others->onTextChanged.connect([this, i](const std::string& t) {
            if (i < m_config.manufacturers().size()) m_config.manufacturers()[i].akas = split(t);
        });
        m_rows->addChildWidget(std::move(row));
    }
}

void JPlacerManufacturersDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    if (m_refill) {
        m_refill = false;
        fill();
    }
    m_note->setMinimumSize(0.f, std::max(st.labelHeight, m_note->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
