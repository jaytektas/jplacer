// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerChildFiducialsDialog.h"

#include "model/JPSides.h"
#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cmath>
#include <limits>

inline namespace jf {

namespace {

struct Point {
    double x, y;
    size_t index;   // its placement in the list
};

// OpenPnP's QuickHull, which these lists are ordered by.
double pointLocation(const Point& a, const Point& b, const Point& p) {
    const double cp = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    return cp > 0 ? 1 : -1;
}

double distance(const Point& a, const Point& b, const Point& c) {
    return std::fabs((b.x - a.x) * (a.y - c.y) - (b.y - a.y) * (a.x - c.x));
}

void hullSet(const Point& a, const Point& b, std::vector<Point> set, std::vector<Point>& hull) {
    auto at = [&hull](const Point& p) {
        for (size_t i = 0; i < hull.size(); ++i)
            if (hull[i].index == p.index) return i;
        return hull.size();
    };
    const size_t insert = at(b);
    if (set.empty()) return;
    if (set.size() == 1) {
        hull.insert(hull.begin() + long(insert), set[0]);
        return;
    }
    double best = -std::numeric_limits<double>::infinity();
    size_t furthest = 0;
    for (size_t i = 0; i < set.size(); ++i)
        if (const double d = distance(a, b, set[i]); d > best) {
            best = d;
            furthest = i;
        }
    const Point p = set[furthest];
    set.erase(set.begin() + long(furthest));
    hull.insert(hull.begin() + long(insert), p);
    std::vector<Point> leftAP, leftPB;
    for (const Point& m : set)
        if (pointLocation(a, p, m) == 1) leftAP.push_back(m);
    for (const Point& m : set)
        if (pointLocation(p, b, m) == 1) leftPB.push_back(m);
    hullSet(a, p, leftAP, hull);
    hullSet(p, b, leftPB, hull);
}

std::vector<Point> quickHull(std::vector<Point> points) {
    if (points.size() < 3) return points;
    size_t minI = 0, maxI = 0;
    double minX = std::numeric_limits<double>::infinity(), maxX = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].x < minX) {
            minX = points[i].x;
            minI = i;
        }
        if (points[i].x > maxX) {
            maxX = points[i].x;
            maxI = i;
        }
    }
    const Point a = points[minI], b = points[maxI];
    std::vector<Point> hull { a, b };
    std::erase_if(points, [&](const Point& p) { return p.index == a.index || p.index == b.index; });
    std::vector<Point> left, right;
    for (const Point& p : points) (pointLocation(a, b, p) == -1 ? left : right).push_back(p);
    hullSet(a, b, right, hull);
    hullSet(b, a, left, hull);
    return hull;
}

double polygonArea(const std::vector<Point>& v) {
    if (v.size() < 3) return 0;
    double area = 0;
    Point j = v.back();
    for (const Point& i : v) {
        area += (j.x + i.x) * (j.y - i.y);
        j = i;
    }
    return std::fabs(0.5 * area);
}

} // namespace

JPlacerChildFiducialsDialog::JPlacerChildFiducialsDialog(JPConfiguration& config, JPPanelLocation& panel,
                                                         std::function<void(std::vector<std::string>)> onChosen,
                                                         JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow(panel.holder->name.value_or("") + " - Select Child Fiducial(s)/Placement(s) For Panel Alignment", kW, kH,
                    hal, sx, sy, parent)
    , m_panel(panel)
    , m_onChosen(std::move(onChosen))
    , m_model(config, [] { return static_cast<const JPJob*>(nullptr); },
              { JPPlacementsTableModel::kEnabled, JPPlacementsTableModel::kId, JPPlacementsTableModel::kPart,
                JPPlacementsTableModel::kSide, JPPlacementsTableModel::kX, JPPlacementsTableModel::kY,
                JPPlacementsTableModel::kRotation, JPPlacementsTableModel::kType, JPPlacementsTableModel::kComments }) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    m_panel.setParentsOfAllDescendants();
    gather(m_panel);

    m_instructions = std::make_unique<JLabel>(
        g,
        "Select one or more child fiducials and/or placements from the list below to use for panel alignment. Hold "
        "down the Control or Shift keys to select multiple items. Click on the Auto Select button to automatically "
        "select a good set. If one of the of the automatically chosen items is undesirable such as being too large, "
        "disable it and re-click on the Auto Select button.",
        0.f);
    m_instructions->setWordWrap(true);
    add(m_instructions.get());

    m_options = JPUiParts::row(g);
    m_options->add(std::make_unique<JLabel>(g, "Show ", 0.f));
    // A tick box or radio as wide as its words and its mark.
    auto wide = [](const char* label) { return JTextHelper::measureWidth(label) + 2 * JStyle::current().controlHeight; };
    JCheckBox* hullOnly = m_options->add(std::make_unique<JCheckBox>(g, "Hull Only", wide("Hull Only")));
    hullOnly->setChecked(true);
    hullOnly->setTooltip("Show only those fiducials/placements that are on the outermost periphery of the panel (which "
                         "are generally the best ones to use for alignment purposes).");
    hullOnly->onStateChanged.connect([this](bool on) {
        m_hullOnly = on;
        update();
    });
    struct Choice {
        const char* label;
        const char* tooltip;
        Types       types;
    };
    for (const Choice& c : { Choice { "Fiducials", "Use this selection if the plan is to use fiducials to automatically "
                                                    "align the panel.", Types::Fiducials },
                             Choice { "Placements", "Use this selection if the plan is to manually align the panel with "
                                                     "multiple placements.", Types::Placements },
                             Choice { "Both", "", Types::Both } }) {
        JRadioButton* r = m_options->add(std::make_unique<JRadioButton>(g, c.label, wide(c.label)));
        if (*c.tooltip) r->setTooltip(c.tooltip);
        r->setSelected(c.types == m_types);
        const Types types = c.types;
        r->onSelected.connect([this, r, types](bool on) {
            if (!on) return;
            for (JRadioButton* other : m_radios)
                if (other != r) other->setSelected(false);
            m_types = types;
            update();
        });
        m_radios.push_back(r);
    }
    m_options->add(std::make_unique<JContainer>(g, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    JButton* autoSelect = m_options->add(JPUiParts::button(g, "Auto Select"));
    autoSelect->setTooltip("Selects a good set of items to use for panel alignment.");
    autoSelect->onClicked.connect([this] {
        update();
        std::vector<int> rows;
        for (const std::string& id : good())
            if (const int r = m_model.rowOf(id); r >= 0) rows.push_back(r);
        m_table->selectRows(rows);
    });
    add(m_options.get());

    m_model.setOnlyEnabledEditable(true);
    m_model.onChanged = [this] {
        // An item turned on or off stays so whatever is shown.
        for (const JPPlacement& p : m_shown.placements)
            for (JPPlacement& a : m_all)
                if (a.id == p.id) a.enabled = p.enabled;
    };
    m_table = std::make_unique<JPTable>(g);
    m_table->setModel(&m_model);
    m_table->onSelectionChanged.connect([this] { m_ok->setEnabled(!m_table->selectedRows().empty()); });
    add(m_table.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_ok = m_buttons->addButton("OK", JDialogButtonBox::Role::Accept);
    m_ok->setEnabled(false);
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject);
    m_buttons->onAccept.connect([this] {
        std::vector<std::string> ids;
        for (const int r : m_table->selectedRows())
            if (const JPPlacement* p = m_model.placement(r)) ids.push_back(p->id);
        if (ids.empty()) return;
        close();
        if (m_onChosen) m_onChosen(ids);
    });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
    update();
}

void JPlacerChildFiducialsDialog::gather(JPPanelLocation& at) {
    // Each child's placements, as pseudo-placements of the panel.
    const JPPanel* panel = m_panel.panel();
    for (JPPlacementsHolderLocation* child : at.children()) {
        const std::string uid = child->uniqueId();
        for (const JPPlacement& p : child->holder->placements) {
            const std::string id = (uid.empty() ? std::string() : uid + JPPlacementsHolderLocation::kIdDelimiter) + p.id;
            JPPlacement pseudo;
            if (!panel->pseudoPlacementLocation(id, pseudo)) continue;
            pseudo.enabled = true;
            pseudo.side = JPSides::flip(p.side, child->globalSide() == JPSide::Bottom);
            pseudo.comments = "Pseudo-placement for panel alignment only";
            m_all.push_back(pseudo);
        }
        if (child->kind() == JPPlacementsHolderLocation::Kind::Panel) gather(*static_cast<JPPanelLocation*>(child));
    }
}

std::vector<JPPlacement> JPlacerChildFiducialsDialog::filtered() const {
    std::vector<JPPlacement> out;
    for (const JPPlacement& p : m_all) {
        const bool fiducial = p.type == JPPlacement::Type::Fiducial;
        if ((fiducial && m_types != Types::Placements) || (!fiducial && m_types != Types::Fiducials)) out.push_back(p);
    }
    return out;
}

std::vector<JPPlacement> JPlacerChildFiducialsDialog::hull(const std::vector<JPPlacement>& from) const {
    std::vector<JPPlacement> out;
    for (JPSide side : { JPSide::Top, JPSide::Bottom }) {
        std::vector<Point> points;
        for (size_t i = 0; i < from.size(); ++i)
            if (from[i].side == side) {
                const JPLocation l = from[i].location.convertToUnits(JPLengthUnit::Millimeters);
                points.push_back({ l.x(), l.y(), i });
            }
        for (const Point& p : quickHull(points)) out.push_back(from[p.index]);
    }
    return out;
}

std::vector<std::string> JPlacerChildFiducialsDialog::good() const {
    // On each side, of the hull's items turned on, the four (or fewer) enclosing the most area.
    std::vector<std::string> out;
    const std::vector<JPPlacement> candidates = hull(filtered());
    for (JPSide side : { JPSide::Top, JPSide::Bottom }) {
        std::vector<Point> points;
        for (size_t i = 0; i < candidates.size(); ++i)
            if (candidates[i].enabled && candidates[i].side == side) {
                const JPLocation l = candidates[i].location.convertToUnits(JPLengthUnit::Millimeters);
                points.push_back({ l.x(), l.y(), i });
            }
        const size_t n = std::min<size_t>(4, points.size());
        std::vector<Point> best;
        double bestArea = 0;
        bool first = true;
        // Every combination of n, in order.
        std::vector<size_t> idx(n);
        for (size_t i = 0; i < n; ++i) idx[i] = i;
        for (;;) {
            std::vector<Point> pick;
            for (size_t i : idx) pick.push_back(points[i]);
            const double area = polygonArea(pick);
            if (first || area > bestArea) {
                best = pick;
                bestArea = area;
                first = false;
            }
            long k = long(n) - 1;
            while (k >= 0 && idx[size_t(k)] == points.size() - n + size_t(k)) --k;
            if (k < 0) break;
            ++idx[size_t(k)];
            for (size_t i = size_t(k) + 1; i < n; ++i) idx[i] = idx[i - 1] + 1;
        }
        for (const Point& p : best) out.push_back(candidates[p.index].id);
    }
    return out;
}

void JPlacerChildFiducialsDialog::update() {
    const std::vector<JPPlacement> f = filtered();
    m_shown.placements = m_hullOnly ? hull(f) : f;
    m_model.setHolder(&m_shown);
    m_table->refresh();
}

void JPlacerChildFiducialsDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    float y = contentTop();
    const float textH = std::max(st.labelHeight, m_instructions->heightFor(cw));
    m_instructions->setBounds({ pad, y, cw, textH });
    y += textH + st.spacing;
    m_options->setBounds({ pad, y, cw, st.controlHeight });
    graph().invalidateNode(m_options->getNodeId(), DirtySelf);
    graph().computeLayout(m_options->getNodeId(), { cw, cw, st.controlHeight, st.controlHeight });
    y += st.controlHeight + st.spacing;
    m_table->setBounds({ pad, y, cw, std::max(0.f, buttonsY - st.spacing * 2 - y) });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
