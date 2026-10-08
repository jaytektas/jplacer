// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPartPickerDialog.h"

#include "model/JPLengthUnits.h"
#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>

#include <algorithm>
#include <cstdio>

inline namespace jf {

namespace {

// The fields a board part's files gave, as the picker names them, in this order; extras after, by their own names.
const std::vector<std::pair<const char*, const char*>> kShownFields {
    { "value", "Value" }, { "footprint", "Footprint" }, { "package", "Package" }, { "mpn", "MPN" },
    { "manufacturer", "Manufacturer" }, { "supplier", "Supplier" }, { "supplierPn", "Supplier PN" },
    { "description", "Description" }, { "height", "Height" }, { "datasheet", "Datasheet" },
};

std::string heightText(const JPPart& p) {
    if (p.height.value() <= 0) return "";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f %s", p.height.value(), JPLengthUnits::shortName(p.height.units()));
    return buf;
}

} // namespace

JPlacerPartPickerDialog::JPlacerPartPickerDialog(const JPConfiguration& config, const JPBoard& board,
                                                 const std::string& placementId,
                                                 std::function<void(const JPPartChoice&)> chosen, JGpuHal& hal, int sx,
                                                 int sy, NativeWinHandleType parent)
    : JDialogWindow("Part for " + placementId, kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_chosen(std::move(chosen))
    , m_designator(placementId) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    const JPPlacement* placement = board.find(placementId);
    if (const JPBoardPart* bp = placement ? board.part(placement->boardPart) : nullptr) m_part = *bp;
    else if (placement) m_part.fields["part"] = placement->partId;
    m_candidates = JPPartMatcher::candidates(config, m_part);
    std::vector<std::string> others;
    if (!m_part.key.empty())
        for (const std::string& id : board.placementsOf(m_part.key))
            if (id != placementId) others.push_back(id);

    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    auto note = [&](const std::string& text) {
        JLabel* l = m_content->add(std::make_unique<JLabel>(g, text, 0.f));
        l->setWordWrap(true);
        m_notes.push_back(l);
    };

    // Which placements, what the files said, what it is now.
    std::string who = placementId;
    if (!others.empty()) {
        who += " and " + std::to_string(others.size()) + " other placement(s) of the same part (";
        for (size_t i = 0; i < others.size() && i < 8; ++i) who += (i ? ", " : "") + others[i];
        who += others.size() > 8 ? ", …)" : ")";
    }
    note(who);
    std::string said;
    for (const auto& [key, label] : kShownFields)
        if (!m_part.field(key).empty()) said += (said.empty() ? "" : "  ·  ") + std::string(label) + " " + m_part.field(key);
    for (const auto& [key, value] : m_part.fields)
        if (key.rfind("extra:", 0) == 0) said += (said.empty() ? "" : "  ·  ") + key.substr(6) + " " + value;
    note(said.empty() ? "The files said nothing of its part: name " + m_part.field("part") : "The files say: " + said);
    switch (m_part.state) {
        case JPBoardPart::State::Matched: note("Now: the library's " + m_part.libraryPartId); break;
        case JPBoardPart::State::Local:   note("Now: the board's own " + m_part.partId()); break;
        case JPBoardPart::State::Unmatched: note("Now: to be chosen"); break;
    }

    m_filter = m_content->add(std::make_unique<JLineEdit>(g, "Filter the library: words in a part's name, package or description"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setTooltip("Every word must be in the part's name, its package or the package's description; [x] clears it");
    m_filter->onTextChanged.connect([this](const std::string&) { fill(); });
    add(m_filter);

    m_list = m_content->add(std::make_unique<JDataGrid>(g, std::vector<std::string>{ "Part", "Package", "Height", "Why" }));
    m_list->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_list->onSelectionChanged.connect([this](int i) { m_use->setEnabled(i >= 0 && size_t(i) < m_shown.size()); });
    m_list->onRowActivated.connect([this](int i) { m_activated = i; });

    // What the library could remember of it: the names the files give it.
    std::string remember;
    const std::string footprint = !m_part.field("footprint").empty() ? m_part.field("footprint") : m_part.field("package");
    for (const std::string& s : { m_part.field("value"), footprint, m_part.field("mpn"), m_part.field("supplierPn") })
        if (!s.empty()) remember += (remember.empty() ? "" : " ") + s;
    if (!remember.empty()) {
        m_remember = m_content->add(std::make_unique<JCheckBox>(g, "Remember: \"" + remember + "\" is the part used, the next "
                                                                   "time a board calls it so", 0.f));
        m_remember->setChecked(true);
        m_remember->setTooltip("The library part used keeps these names (value and footprint, MPN, supplier's part number), "
                               "so the next import matches it without asking");
    }
    if (!others.empty()) {
        m_onlyThis = m_content->add(std::make_unique<JCheckBox>(g, "Only " + placementId + " (the others keep theirs)", 0.f));
        m_onlyThis->setTooltip("Choose for this placement alone; it gets a part of its own, what the files said kept");
    }
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Leave to Be Chosen", JDialogButtonBox::Role::Action)->onClicked.connect([this] {
        choose(JPPartChoice::Kind::ToBeChosen);
    });
    m_buttons->addButton("Make It the Board's Own", JDialogButtonBox::Role::Action)->onClicked.connect([this] {
        choose(JPPartChoice::Kind::BoardsOwn);
    });
    JButton* addTo = m_buttons->addButton("Add to Library", JDialogButtonBox::Role::Action);
    addTo->setTooltip("Make a library part from what the files say (value, footprint, MPN, manufacturer, supplier, "
                      "description, height) and use it");
    addTo->onClicked.connect([this] { choose(JPPartChoice::Kind::AddToLibrary); });
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject)->setTooltip("Change nothing");
    m_use = m_buttons->addButton("Use This Part", JDialogButtonBox::Role::Accept);
    m_use->setTooltip("The library's part chosen in the list");
    m_buttons->onReject.connect([this] { close(); });
    m_buttons->onAccept.connect([this] { choose(JPPartChoice::Kind::Library); });
    add(m_buttons.get());
    fill();
}

void JPlacerPartPickerDialog::fill() {
    const std::vector<std::string> words = JPPartMatcher::words(m_filter->text());
    m_shown.clear();
    std::vector<std::string> why;
    for (const auto& c : m_candidates)
        if (words.empty() || JPPartMatcher::matches(m_config, *c.part, words)) {
            m_shown.push_back(c.part);
            why.push_back(c.why);
        }
    // Filtering (or nothing to suggest): the rest of the library that fits, by name.
    if (!words.empty() || m_candidates.empty()) {
        std::vector<const JPPart*> rest;
        for (const auto& p : m_config.parts())
            if (std::find(m_shown.begin(), m_shown.end(), p.get()) == m_shown.end()
                && (words.empty() || JPPartMatcher::matches(m_config, *p, words)))
                rest.push_back(p.get());
        std::sort(rest.begin(), rest.end(), [](const JPPart* a, const JPPart* b) { return a->id < b->id; });
        for (const JPPart* p : rest) {
            m_shown.push_back(p);
            why.push_back("");
        }
    }
    std::vector<std::vector<std::string>> rows;
    for (size_t i = 0; i < m_shown.size(); ++i) rows.push_back({ m_shown[i]->id, m_shown[i]->packageId, heightText(*m_shown[i]), why[i] });
    m_list->setRows(rows);
    m_list->scrollToTop();
    // The one it is now, else the best candidate; none when the list holds no candidate (nothing is suggested,
    // so Return must not take whichever part sorts first).
    int select = !m_shown.empty() && !why.empty() && !why.front().empty() ? 0 : -1;
    for (size_t i = 0; i < m_shown.size(); ++i)
        if (m_part.state == JPBoardPart::State::Matched && m_shown[i]->id == m_part.libraryPartId) select = int(i);
    m_list->setSelectedIndex(select);
    m_use->setEnabled(select >= 0);
}

void JPlacerPartPickerDialog::choose(JPPartChoice::Kind kind) {
    JPPartChoice c;
    c.kind = kind;
    c.onlyThis = m_onlyThis && m_onlyThis->isChecked();
    c.learn = m_remember && m_remember->isChecked();
    if (kind == JPPartChoice::Kind::Library) {
        const int i = m_list->selectedIndex();
        if (i < 0 || size_t(i) >= m_shown.size()) return;
        c.libraryId = m_shown[size_t(i)]->id;
    }
    close();
    if (m_chosen) m_chosen(c);
}

void JPlacerPartPickerDialog::onMouse(float, float, bool pressed, bool, bool) {
    // Typing goes to the filter from the start.
    if (!m_focusPlaced) {
        m_focusPlaced = true;
        m_filter->requestFocus();
    }
    // A row activated by Return (no press this frame) or a double-click is used; a single click only chose it.
    if (m_activated >= 0) {
        const int row = m_activated;
        m_activated = -1;
        if ((!pressed || JWidget::s_doubleClick) && row == m_list->selectedIndex()) choose(JPPartChoice::Kind::Library);
    }
}

void JPlacerPartPickerDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    for (JLabel* n : m_notes) n->setMinimumSize(0.f, std::max(st.labelHeight, n->heightFor(cw)));
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
    const float partW = cw * 0.35f, packageW = cw * 0.2f, heightW = JTextHelper::measureWidth("00.00 mm") + 2 * st.gridCellPadding;
    m_list->setColumnWidths({ partW, packageW, heightW, std::max(0.f, cw - partW - packageW - heightW - st.scrollBarWidth) });
}

} // inline namespace jf
