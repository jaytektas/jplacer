// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPanelArrayDialog.h"

#include "model/JPBoardLocation.h"
#include "model/JPDefinitionChanges.h"
#include "ui/JPFieldGrid.h"
#include "ui/JPTextField.h"
#include "ui/JPUiParts.h"

#include <j/core/JCheckBox.h>
#include <j/core/JLabel.h>
#include <j/core/JRadioButton.h>
#include <j/core/JSpinBox.h>
#include <j/core/JStyle.h>

#include <climits>
#include <cmath>
#include <cstdio>

inline namespace jf {

namespace {

std::string mm(const JPLength& l) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3f", l.convertToUnits(JPLengthUnit::Millimeters).value());
    return buf;
}

std::string arrayId(const std::string& id, int row, int column) {
    return id + "[" + std::to_string(row) + "," + std::to_string(column) + "]";
}

} // namespace

JPlacerPanelArrayDialog::JPlacerPanelArrayDialog(JPConfiguration& config, const JPJob* job, JPPanelLocation& panel,
                                                 JPPlacementsHolderLocation& child, std::function<void()> onDone,
                                                 JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Panel Array Generator", kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_job(job)
    , m_panel(panel)
    , m_child(child)
    , m_childId(child.id)
    , m_onDone(std::move(onDone)) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_top = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_top->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);

    auto spin = [&](int& value, const char* tip) {
        auto s = std::make_unique<JSpinBox>(g, 1, INT_MAX, 0.f);
        s->setValue(value);
        s->setTooltip(tip);
        s->onValueChanged.connect([this, &value](int v) {
            value = v;
            generate();
        });
        return s;
    };
    auto length = [&](JPLength& value, const char* tip) {
        auto f = std::make_unique<JPTextField>(g);
        f->setValue(mm(value));
        f->setTooltip(tip);
        JPTextField* raw = f.get();
        raw->onCommitted.connect([this, raw, &value](std::string text) {
            if (const auto l = JPLength::parseWithDefaultUnits(text, JPLengthUnit::Millimeters)) {
                value = *l;
                generate();
            }
            raw->setValue(mm(value));
        });
        return f;
    };
    auto pair = [&](std::unique_ptr<JWidget> a, std::unique_ptr<JWidget> b) {
        auto row = JPUiParts::row(g);
        a->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        b->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        row->add(std::move(a));
        row->add(std::move(b));
        return row;
    };

    // The array's type.
    auto typeGrid = std::make_unique<JPFieldGrid>(g, 1);
    auto type = std::make_unique<JComboBox>(g, std::vector<std::string> { "Rectangular", "Circular" }, 0.f);
    type->setTooltip("Selects the type of array to create");
    type->setCurrentIndex(0);
    type->onIndexChanged.connect([this](int i) {
        m_circular = i == 1;
        showPage();
        generate();
    });
    typeGrid->widget("Array Type", std::move(type));
    typeGrid->setVSizePolicy(JSizePolicyMode::Fixed);
    typeGrid->setSize(0.f, typeGrid->rowsHeight());
    m_typeRow = std::move(typeGrid);

    // Rectangular: columns and rows.
    auto rect = std::make_unique<JPFieldGrid>(g, 1);
    rect->widget("", pair(std::make_unique<JLabel>(g, "Columns (X)", 0.f), std::make_unique<JLabel>(g, "Rows (Y)", 0.f)));
    rect->widget("Count", pair(spin(m_columns, "Number of columns in the array"), spin(m_rows, "Number of rows in the array")));
    rect->widget("Step", pair(length(m_columnStep, "The distance from a point in one column to the same point in the next column"),
                              length(m_rowStep, "The distance from a point in one row to the same point in the next row")));
    auto alternate = std::make_unique<JComboBox>(g,
        std::vector<std::string> { "the same number of columns as the first row", "one more column than the first row",
                                   "one less column than the first row" }, 0.f);
    alternate->setTooltip("Selects the number of columns in the even rows of the array");
    alternate->setCurrentIndex(0);
    alternate->onIndexChanged.connect([this](int i) {
        m_alternateDelta = i == 1 ? 1 : i == 2 ? -1 : 0;
        generate();
    });
    rect->widget("Alternate rows have", std::move(alternate));
    rect->widget("Alternate rows have",
                 pair(length(m_alternateOffset, "The amount to offset the columns of the alternate rows relative to the "
                                                "columns of the first row"),
                      std::make_unique<JLabel>(g, "column offset relative to the first row", 0.f)));
    rect->setVSizePolicy(JSizePolicyMode::Fixed);
    rect->setSize(0.f, rect->rowsHeight());
    m_rectangular = std::move(rect);

    // Circular: round a centre.
    auto round = std::make_unique<JPFieldGrid>(g, 1);
    round->widget("", pair(std::make_unique<JLabel>(g, "X", 0.f), std::make_unique<JLabel>(g, "Y", 0.f)));
    round->widget("Array Center", pair(length(m_centerX, "The X coordinate of the array center"),
                                       length(m_centerY, "The Y coordinate of the array center")));
    auto panelFrame = std::make_unique<JRadioButton>(g, "Panel", 0.f);
    auto childFrame = std::make_unique<JRadioButton>(g, "Root Child", 0.f);
    panelFrame->setTooltip("The Array Center coordinates are expressed in the Panel's reference frame");
    childFrame->setTooltip("The Array Center Coordinates are expressed in the root child's reference frame");
    panelFrame->setSelected(true);
    JRadioButton* pf = panelFrame.get();
    JRadioButton* cf = childFrame.get();
    pf->onSelected.connect([this, cf](bool on) {
        if (!on) return;
        cf->setSelected(false);
        m_panelFrame = true;
        generate();
    });
    cf->onSelected.connect([this, pf](bool on) {
        if (!on) return;
        pf->setSelected(false);
        m_panelFrame = false;
        generate();
    });
    round->widget("Relative to", pair(std::move(panelFrame), std::move(childFrame)));
    auto proportional = std::make_unique<JCheckBox>(g, "Increase proportionally with radius", 0.f);
    proportional->setTooltip("When selected, the number of angular steps at each radial step is increased "
                             "proportionally to its radius");
    proportional->onStateChanged.connect([this](bool on) {
        m_proportional = on;
        generate();
    });
    round->widget("Angular Steps", pair(spin(m_angular, "Number of copies to generate angularly around the Array Center"),
                                        std::move(proportional)));
    round->widget("Radial Steps", spin(m_radial, "Number of copies to generate radially outward from the Array Center"));
    round->setVSizePolicy(JSizePolicyMode::Fixed);
    round->setSize(0.f, round->rowsHeight());
    m_round = std::move(round);

    // The panel, the copies dashed short and the child copied dashed long.
    m_viewer = std::make_unique<JPPlacementsViewer>(g, &m_panel, false);
    m_viewer->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_viewer->canvas().arrayRoot = &m_child;
    m_viewer->setShown(&JPPlacementsViewerCanvas::showLocations, false);
    m_viewer->setShown(&JPPlacementsViewerCanvas::showReticle, false);
    showPage();
    add(m_top.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("OK", JDialogButtonBox::Role::Accept);
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject);
    m_buttons->onAccept.connect([this] { finish(); });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
    generate();
}

JPlacerPanelArrayDialog::~JPlacerPanelArrayDialog() {
    // Closed without OK: as it was.
    if (!m_done) {
        removeCopies();
        restoreRoot();
    }
    m_top->clear();   // its parts are owned here, and go first
}

std::unique_ptr<JPPlacementsHolderLocation> JPlacerPanelArrayDialog::copyOfRoot() const {
    std::unique_ptr<JPPlacementsHolderLocation> copy;
    if (m_child.kind() == JPPlacementsHolderLocation::Kind::Board) copy = std::make_unique<JPBoardLocation>();
    else copy = std::make_unique<JPPanelLocation>();
    copy->holder = m_child.holder->instance();
    copy->checkFiducials = m_child.checkFiducials;
    copy->side = m_child.side;
    copy->parent = &m_panel;
    return copy;
}

void JPlacerPanelArrayDialog::removeCopies() {
    for (const JPPlacementsHolderLocation* c : m_copies) m_panel.panel()->removeChild(c);
    m_copies.clear();
}

void JPlacerPanelArrayDialog::restoreRoot() { m_child.id = m_childId; }

void JPlacerPanelArrayDialog::generate() {
    removeCopies();
    restoreRoot();
    JPPanel& def = *m_panel.panel();
    auto addCopy = [&](const std::string& id, const JPLocation& at) {
        auto c = copyOfRoot();
        c->id = id;
        c->setLocation(at);
        m_copies.push_back(def.addChild(std::move(c)));
    };
    const JPLocation root = m_child.location();
    if (!m_circular) {
        for (int i = 0; i < m_rows; ++i) {
            const int columns = m_columns + (i % 2 == 0 ? 0 : m_alternateDelta);
            const double rowOffset = std::fabs(m_alternateOffset.convertToUnits(JPLengthUnit::Millimeters).value()) *
                                     (i % 2 == 0 ? 0 : -m_alternateDelta);
            for (int j = 0; j < columns; ++j) {
                if (i == 0 && j == 0) {
                    if (m_rows > 1 || columns > 1) m_child.id = arrayId(m_childId, 1, 1);
                    continue;
                }
                const JPLocation offset(JPLengthUnit::Millimeters,
                                        m_columnStep.convertToUnits(JPLengthUnit::Millimeters).value() * j + rowOffset,
                                        m_rowStep.convertToUnits(JPLengthUnit::Millimeters).value() * i, 0, 0);
                addCopy(arrayId(m_childId, i + 1, j + 1), root.add(offset));
            }
        }
    } else {
        JPLocation center(JPLengthUnit::Millimeters, m_centerX.convertToUnits(JPLengthUnit::Millimeters).value(),
                          m_centerY.convertToUnits(JPLengthUnit::Millimeters).value(),
                          root.lengthZ().convertToUnits(JPLengthUnit::Millimeters).value(), 0);
        if (!m_panelFrame) center = m_child.placementLocation(center, true);
        const JPLocation r = root.convertToUnits(JPLengthUnit::Millimeters);
        const JPLocation cm = center.convertToUnits(JPLengthUnit::Millimeters);
        const double radiusStep = std::hypot(r.x() - cm.x(), r.y() - cm.y());
        const double initAngle = std::atan2(r.y() - cm.y(), r.x() - cm.x()) * 180 / M_PI;
        int count = m_angular;
        for (int i = 0; i < m_radial; ++i) {
            const double radius = radiusStep * (i + 1);
            double step = 360.0 / m_angular;
            if (m_proportional) step /= (i + 1);
            for (int j = 0; j < count; ++j) {
                if (i == 0 && j == 0 && (count > 1 || m_radial > 1)) {
                    m_child.id = arrayId(m_childId, 1, 1);
                    continue;
                }
                // OpenPnP puts a copy on top of the child itself here (one step each way); left out.
                if (i == 0 && j == 0) continue;
                const double a = (initAngle + step * j) * M_PI / 180;
                const JPLocation at(JPLengthUnit::Millimeters, cm.x() + radius * std::cos(a), cm.y() + radius * std::sin(a),
                                    cm.z(), r.rotation() + step * j);
                addCopy(arrayId(m_childId, i + 1, j + 1), at.convertToUnits(root.units()));
            }
            if (m_proportional) count += m_angular;
        }
    }
    m_panel.setParentsOfAllDescendants();
    m_viewer->canvas().arrayMembers.assign(m_copies.begin(), m_copies.end());
    m_viewer->regenerate();
}

void JPlacerPanelArrayDialog::finish() {
    // The copies made the panel's for good, and given to each use of it.
    m_done = true;
    JPPanel& def = *m_panel.panel();
    std::vector<std::unique_ptr<JPPlacementsHolderLocation>> made;
    for (const JPPlacementsHolderLocation* c : m_copies) {
        auto copy = c->instance();
        copy->makeDefinition();
        made.push_back(std::move(copy));
    }
    const std::string renamed = m_child.id;
    removeCopies();
    restoreRoot();
    JPDefinitionChanges changes(m_config, m_job);
    if (renamed != m_childId) changes.child(def, m_childId, [&renamed](JPPlacementsHolderLocation& c) { c.id = renamed; });
    for (auto& c : made) changes.childAdded(def, std::move(c));
    m_panel.setParentsOfAllDescendants();
    close();
    if (m_onDone) m_onDone();
}

void JPlacerPanelArrayDialog::showPage() {
    // One page at a time (OpenPnP's card layout): the content laid out again with it.
    m_top->clear();
    m_top->add(m_typeRow.get());
    m_top->add(m_circular ? m_round.get() : m_rectangular.get());
    m_top->add(m_viewer.get());
}

void JPlacerPanelArrayDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_top->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_top->getNodeId(), DirtySelf);
    const JRect b = m_top->bounds();
    graph().computeLayout(m_top->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
