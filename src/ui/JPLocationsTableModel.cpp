// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLocationsTableModel.h"

#include "JPLengthCell.h"

#include "model/JPDefinitionChanges.h"
#include "model/JPSides.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

const JPSide kSides[] = { JPSide::Bottom, JPSide::Top };

std::string rotationText(double r) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3f", r);
    return buf;
}

bool parseNumber(const std::string& text, double& out) {
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    while (end && *end && std::isspace(uint8_t(*end))) ++end;
    return !text.empty() && end && !*end;
}

} // namespace

JPLocationsTableModel::JPLocationsTableModel(JPConfiguration& config, Mode mode, std::vector<Col> shown,
                                             std::function<const JPJob*()> job)
    : m_config(config), m_mode(mode), m_shown(std::move(shown)), m_job(std::move(job)) {}

void JPLocationsTableModel::setRows(JPPanelLocation* root, std::vector<JPPlacementsHolderLocation*> rows) {
    m_root = root;
    m_rows = std::move(rows);
}

JPPlacementsHolderLocation* JPLocationsTableModel::location(int row) const {
    return row >= 0 && size_t(row) < m_rows.size() ? m_rows[size_t(row)] : nullptr;
}

int JPLocationsTableModel::rowOf(const JPPlacementsHolderLocation* l) const {
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i] == l) return int(i);
    return -1;
}

JPTableModel::Column JPLocationsTableModel::column(int c) const {
    Column col;
    col.align = Align::Center;
    switch (m_shown[size_t(c)]) {
        case kId:        col.name = "Board/Panel Id"; col.align = Align::Left; break;
        case kName:      col.name = "Name"; col.align = Align::Left; break;
        case kWidth:     col.name = "Width"; col.decimalAligned = true; break;
        case kLength:    col.name = "Length"; col.decimalAligned = true; break;
        case kSide:      col.name = "Side"; col.kind = Kind::Choice; break;
        case kX:         col.name = "X"; col.decimalAligned = true; break;
        case kY:         col.name = "Y"; col.decimalAligned = true; break;
        case kZ:         col.name = "Z"; col.decimalAligned = true; break;
        case kRotation:  col.name = "Rot."; col.decimalAligned = true; break;
        case kEnabled:   col.name = "Enabled?"; col.kind = Kind::Boolean; break;
        case kCheckFids: col.name = "Check Fids?"; col.kind = Kind::Boolean; break;
        case kColumns:   break;
    }
    return col;
}

std::string JPLocationsTableModel::rowKey(int row) const {
    const JPPlacementsHolderLocation* l = location(row);
    return l ? l->uniqueId() : std::string();
}

std::string JPLocationsTableModel::text(int row, int c) const {
    const JPPlacementsHolderLocation* l = location(row);
    if (!l || !l->holder) return {};
    const JPLocation g = l->globalLocation();
    switch (m_shown[size_t(c)]) {
        case kId:       return l->uniqueId();
        case kName:     return l->holder->name.value_or("");
        case kWidth:    return JPLengthCell::text(l->holder->dimensions.lengthX(), true);
        case kLength:   return JPLengthCell::text(l->holder->dimensions.lengthY(), true);
        case kSide:     return JPSides::name(l->globalSide());
        case kX:        return JPLengthCell::text(g.lengthX(), true);
        case kY:        return JPLengthCell::text(g.lengthY(), true);
        case kZ:        return JPLengthCell::text(g.lengthZ(), true);
        case kRotation: return rotationText(g.rotation());
        default:        return {};
    }
}

bool JPLocationsTableModel::checked(int row, int c) const {
    const JPPlacementsHolderLocation* l = location(row);
    if (!l) return false;
    if (m_shown[size_t(c)] == kEnabled) return l->isEnabled();
    if (m_shown[size_t(c)] == kCheckFids) return l->checkFiducials;
    return false;
}

std::optional<int> JPLocationsTableModel::compare(int a, int b, int c) const {
    const JPPlacementsHolderLocation *x = location(a), *y = location(b);
    auto cmp = [](double u, double v) { return u < v ? -1 : u > v ? 1 : 0; };
    const JPLocation gx = x->globalLocation(), gy = y->globalLocation();
    switch (m_shown[size_t(c)]) {
        case kWidth:    return cmp(x->holder->dimensions.x(), y->holder->dimensions.x());
        case kLength:   return cmp(x->holder->dimensions.y(), y->holder->dimensions.y());
        case kSide:     return cmp(x->globalSide() == JPSide::Top, y->globalSide() == JPSide::Top);
        case kX:        return cmp(gx.x(), gy.x());
        case kY:        return cmp(gx.y(), gy.y());
        case kZ:        return cmp(gx.z(), gy.z());
        case kRotation: return cmp(gx.rotation(), gy.rotation());
        default:        return std::nullopt;
    }
}

bool JPLocationsTableModel::editable(int row, int c) const {
    const JPPlacementsHolderLocation* l = location(row);
    if (!l) return false;
    const Col col = m_shown[size_t(c)];
    if (m_mode == Mode::PanelDefinition) return col <= kName || col >= kSide;
    // The job's: as OpenPnP's Job tab.
    const bool topLevel = l->parent == m_root;
    if (col == kId) return topLevel;
    if (col == kName) return false;
    if (topLevel || col == kEnabled || col == kCheckFids) {
        const JPJob* job = m_job();
        if ((col == kWidth || col == kLength) && job && job->instanceCount(*l->holder) > 1) return false;
        return true;
    }
    return false;
}

std::vector<std::string> JPLocationsTableModel::choices(int, int c) const {
    std::vector<std::string> out;
    if (m_shown[size_t(c)] == kSide)
        for (JPSide s : kSides) out.push_back(JPSides::name(s));
    return out;
}

void JPLocationsTableModel::edit(JPPlacementsHolderLocation* l, const std::function<void(JPPlacementsHolderLocation&)>& set) {
    if (!l) return;
    if (m_mode == Mode::PanelDefinition && m_root && m_root->panel()) {
        const std::string id = l->id;   // a copy: the change may be to the id
        JPDefinitionChanges(m_config, m_job()).child(*m_root->panel(), id, set);
    } else {
        set(*l);
    }
    if (onChanged) onChanged();
}

void JPLocationsTableModel::setSide(JPPlacementsHolderLocation* l, JPSide side) {
    if (!l || side == l->globalSide()) return;
    // Turned over where it lies (its own panel's children), its transform worked out again.
    const JPLocation saved = l->globalLocation();
    l->setGlobalSide(side);
    if (l->parent == m_root) l->setGlobalLocation(saved);
    l->setLocalToParentTransform(std::nullopt);
    const JPSide local = l->side;
    const JPLocation at = l->location();
    edit(l, [local, at](JPPlacementsHolderLocation& c) {
        c.side = local;
        c.setLocation(at);
        c.setLocalToParentTransform(std::nullopt);
    });
}

bool JPLocationsTableModel::setText(int row, int c, const std::string& text, std::string&) {
    JPPlacementsHolderLocation* l = location(row);
    if (!l || !l->holder) return false;
    switch (m_shown[size_t(c)]) {
        case kId: {
            // Not one another row already has.
            const std::string prefix = l->parent && !l->parent->uniqueId().empty()
                                           ? l->parent->uniqueId() + JPPlacementsHolderLocation::kIdDelimiter
                                           : std::string();
            for (const JPPlacementsHolderLocation* other : m_rows)
                if (other != l && other->uniqueId() == prefix + text) return false;
            const std::string old = l->id;
            edit(l, [&text](JPPlacementsHolderLocation& x) { x.id = text; });
            // The panel's pseudo-placements from it follow its new id.
            if (m_mode == Mode::PanelDefinition && m_root && m_root->panel()) {
                JPPanel& def = *m_root->panel();
                const std::string from = old + JPPlacementsHolderLocation::kIdDelimiter;
                for (std::string& id : def.pseudoPlacementIds)
                    if (id.rfind(from, 0) == 0) id = text + JPPlacementsHolderLocation::kIdDelimiter + id.substr(from.size());
                JPDefinitionChanges(m_config, m_job()).pseudoPlacementsChanged(def);
            }
            return true;
        }
        case kName: {
            JPPlacementsHolder* def = m_config.definitionOf(*l->holder);
            if (!def) return false;
            JPDefinitionChanges(m_config, m_job()).holder(*def, [&text](JPPlacementsHolder& h) { h.name = text; });
            if (onChanged) onChanged();
            return true;
        }
        case kWidth:
        case kLength: {
            const auto length = JPLength::parse(text);
            JPPlacementsHolder* def = m_config.definitionOf(*l->holder);
            if (!length || !def) return false;
            const JPLocation dims = def->dimensions.withField(
                m_shown[size_t(c)] == kWidth ? JPLocation::Field::X : JPLocation::Field::Y, *length, false);
            JPDefinitionChanges(m_config, m_job()).holder(*def, [&dims](JPPlacementsHolder& h) { h.setDimensions(dims); });
            if (onChanged) onChanged();
            return true;
        }
        case kX:
        case kY:
        case kZ: {
            const auto length = JPLength::parse(text);
            if (!length) return false;
            const Col col = m_shown[size_t(c)];
            const JPLocation global = l->globalLocation().withField(
                col == kX ? JPLocation::Field::X : col == kY ? JPLocation::Field::Y : JPLocation::Field::Z, *length, false);
            l->setGlobalLocation(global);
            const JPLocation at = l->location();
            edit(l, [at](JPPlacementsHolderLocation& x) { x.setLocation(at); });
            return true;
        }
        case kRotation: {
            double r;
            if (!parseNumber(text, r)) return false;
            l->setGlobalLocation(l->globalLocation().derive(std::nullopt, std::nullopt, std::nullopt, r));
            const JPLocation at = l->location();
            edit(l, [at](JPPlacementsHolderLocation& x) { x.setLocation(at); });
            return true;
        }
        default: return false;
    }
}

void JPLocationsTableModel::setChoice(int row, int c, int index) {
    if (m_shown[size_t(c)] == kSide && index >= 0 && index < 2) setSide(location(row), kSides[index]);
}

void JPLocationsTableModel::setChecked(int row, int c, bool on) {
    JPPlacementsHolderLocation* l = location(row);
    if (!l) return;
    if (m_shown[size_t(c)] == kEnabled) {
        // Only under a branch that is itself enabled.
        if (l->parent && !l->parent->isEnabled()) return;
        edit(l, [on](JPPlacementsHolderLocation& x) { x.locallyEnabled = on; });
    } else if (m_shown[size_t(c)] == kCheckFids) {
        edit(l, [on](JPPlacementsHolderLocation& x) { x.checkFiducials = on; });
    }
}

} // inline namespace jf
