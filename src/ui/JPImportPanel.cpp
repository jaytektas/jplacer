// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImportPanel.h"

#include "JPUiParts.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

inline namespace jf {

namespace {

std::string mm(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

bool number(const std::string& t, double& out) {
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end != t.c_str();
}

} // namespace

JPImportPanel::JPImportPanel(JSceneGraph& graph, const std::vector<std::string>& tools) : JContainer(graph) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();

    // What is waiting to be accepted.
    auto bar = JPUiParts::row(graph);
    m_pending = bar->add(std::make_unique<JLabel>(graph, ""));
    m_pending->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_pending->setWordWrap(true);
    m_accept = bar->add(JPUiParts::button(graph, "Accept"));
    m_accept->onClicked.connect([this] { if (onAccept) onAccept(); });
    m_discard = bar->add(JPUiParts::button(graph, "Discard"));
    m_discard->onClicked.connect([this] { if (onDiscard) onDiscard(); });
    add(std::move(bar));

    auto body = std::make_unique<JContainer>(graph);
    body->setDirection(JFlexDirection::JRow)->setGap(2 * st.spacing)->setAlignItems(JAlignItems::Stretch);
    body->setVSizePolicy(JSizePolicyMode::Expanding, 1);

    // The sources, the origin and the outline.
    auto side = std::make_unique<JContainer>(graph);
    side->setDirection(JFlexDirection::Column)->setGap(st.spacing)->setAlignItems(JAlignItems::Stretch);
    side->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    // A column of controls: as wide as its buttons need, the rest to the drawing.
    side->setMaximumSize(14 * st.buttonHeight, std::numeric_limits<float>::max());
    side->add(std::make_unique<JLabel>(graph, "Pick-and-place file"));
    m_placementsFile = side->add(std::make_unique<JLabel>(graph, ""));
    m_placementsState = side->add(std::make_unique<JLabel>(graph, ""));
    auto toolRow = JPUiParts::row(graph);
    toolRow->add(std::make_unique<JLabel>(graph, "Written by"));
    m_tool = toolRow->add(std::make_unique<JComboBox>(graph, tools));
    m_tool->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_tool->onIndexChanged.connect([this](int i) { if (!m_updating && onTool && i >= 0) onTool(i); });
    side->add(std::move(toolRow));
    auto cplButtons = JPUiParts::row(graph);
    cplButtons->add(JPUiParts::button(graph, "Choose\xE2\x80\xA6"))->onClicked.connect([this] { if (onChoosePlacements) onChoosePlacements(); });
    m_rereadPlacements = cplButtons->add(JPUiParts::button(graph, "Read Again"));
    m_rereadPlacements->onClicked.connect([this] { if (onRereadPlacements) onRereadPlacements(); });
    side->add(std::move(cplButtons));

    side->add(std::make_unique<JLabel>(graph, "BOM"));
    m_bomFile = side->add(std::make_unique<JLabel>(graph, ""));
    m_bomState = side->add(std::make_unique<JLabel>(graph, ""));
    auto bomButtons = JPUiParts::row(graph);
    bomButtons->add(JPUiParts::button(graph, "Choose\xE2\x80\xA6"))->onClicked.connect([this] { if (onChooseBom) onChooseBom(); });
    m_rereadBom = bomButtons->add(JPUiParts::button(graph, "Read Again"));
    m_rereadBom->onClicked.connect([this] { if (onRereadBom) onRereadBom(); });
    m_removeBom = bomButtons->add(JPUiParts::button(graph, "Remove"));
    m_removeBom->onClicked.connect([this] { if (onRemoveBom) onRemoveBom(); });
    side->add(std::move(bomButtons));

    auto pair = [&](const char* label, JPTextField*& a, JPTextField*& b) {
        auto r = JPUiParts::row(graph);
        auto l = std::make_unique<JLabel>(graph, label, 0.f);
        r->add(std::move(l));
        a = r->add(std::make_unique<JPTextField>(graph));
        b = r->add(std::make_unique<JPTextField>(graph));
        a->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        b->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        return r;
    };
    side->add(std::make_unique<JLabel>(graph, "Origin, in the file's mm (X, Y)"));
    side->add(pair("At", m_originX, m_originY));
    auto origin = [this](std::string) {
        double x, y;
        if (onOrigin && number(m_originX->text(), x) && number(m_originY->text(), y)) onOrigin(x, y);
    };
    m_originX->onCommitted.connect(origin);
    m_originY->onCommitted.connect(origin);
    side->add(JPUiParts::button(graph, "At the Parts' Bottom-Left"))->onClicked.connect([this] { if (onOriginAtParts) onOriginAtParts(); });
    side->add(std::make_unique<JLabel>(graph, "Outline from the origin, mm (width, height)"));
    side->add(pair("Size", m_width, m_height));
    auto outline = [this](std::string) {
        double w, h;
        if (onOutline && number(m_width->text(), w) && number(m_height->text(), h)) onOutline(w, h);
    };
    m_width->onCommitted.connect(outline);
    m_height->onCommitted.connect(outline);
    auto outlineButtons = JPUiParts::row(graph);
    outlineButtons->add(JPUiParts::button(graph, "Around the Parts"))->onClicked.connect([this] { if (onOutlineAroundParts) onOutlineAroundParts(); });
    outlineButtons->add(JPUiParts::button(graph, "None"))->onClicked.connect([this] { if (onNoOutline) onNoOutline(); });
    side->add(std::move(outlineButtons));
    body->add(std::move(side));

    m_board = body->add(std::make_unique<JPBoardView>(graph));
    m_board->setHSizePolicy(JSizePolicyMode::Expanding, 3);
    m_report = body->add(std::make_unique<JListView>(graph));
    m_report->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_report->onItemActivated.connect([this](int i) { if (onReportLine) onReportLine(i); });
    add(std::move(body));
}

void JPImportPanel::showPending(const std::string& text, bool canAccept, bool canDiscard) {
    m_pending->setText(text);
    m_accept->setEnabled(canAccept);
    m_discard->setEnabled(canDiscard);
}

void JPImportPanel::showSources(const Source& placements, int tool, const Source& bom) {
    m_updating = true;
    m_placementsFile->setText(placements.file.empty() ? "none chosen" : placements.file);
    m_placementsState->setText(placements.state);
    m_tool->setCurrentIndex(tool);
    m_rereadPlacements->setEnabled(placements.canReread);
    m_bomFile->setText(bom.file.empty() ? "none" : bom.file);
    m_bomState->setText(bom.state);
    m_rereadBom->setEnabled(bom.canReread);
    m_removeBom->setEnabled(!bom.file.empty());
    m_updating = false;
}

void JPImportPanel::showFrame(double originX, double originY, double width, double height) {
    m_originX->setValue(mm(originX));
    m_originY->setValue(mm(originY));
    m_width->setValue(width > 0 ? mm(width) : "");
    m_height->setValue(height > 0 ? mm(height) : "");
}

void JPImportPanel::showBoard(const JPBoard& board, const JPBoardFrame& frame) {
    m_board->show(board, frame);
}

void JPImportPanel::showReport(const std::vector<std::string>& lines) {
    m_report->setItems(lines);
}

} // inline namespace jf
