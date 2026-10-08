// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementsTableModel.h"

#include "JPLengthCell.h"

#include "model/JPDefinitionChanges.h"
#include "model/JPPanel.h"
#include "model/JPSides.h"

#include <j/core/JStyle.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

constexpr const char* kRankTooltip =
    "Rank controls the order in which placements are handled during a job. Placement are placed from low rank to "
    "high rank. There is a strict ordering when the difference between ranks is 10 or more. Placements in rank R "
    "must all be finished and correct before rank R+10 is considered for placement. There is no ordering "
    "requirement when the difference between ranks is less than 10. Placements at rank R may be placed anywhere "
    "between R-9 and R+9 given other job optimisation priorities.";

// The menus' orders, as OpenPnP's enums and combo boxes have them.
const JPSide kSides[] = { JPSide::Bottom, JPSide::Top };
const JPPlacement::Type kTypes[] = { JPPlacement::Type::Placement, JPPlacement::Type::Fiducial };
const JPPlacement::ErrorHandling kErrorHandlings[] = { JPPlacement::ErrorHandling::Default,
                                                       JPPlacement::ErrorHandling::Alert,
                                                       JPPlacement::ErrorHandling::Defer };

int sideOrder(JPSide s) { return s == JPSide::Bottom ? 0 : 1; }

std::string rotationText(double r) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3f", r);
    return buf;
}

int firstDigit(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (std::isdigit(uint8_t(s[i]))) return int(i);
    return -1;
}

// The digits of `s`, wherever they are, as one number.
long long digitsOf(const std::string& s) {
    long long n = 0;
    for (char c : s)
        if (std::isdigit(uint8_t(c))) n = std::min(n * 10 + (c - '0'), 999999999999999LL);
    return n;
}

int byteCompare(const std::string& a, const std::string& b) {
    const int c = a.compare(b);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

} // namespace

JPPlacementsTableModel::JPPlacementsTableModel(JPConfiguration& config, std::function<const JPJob*()> job,
                                               std::vector<Col> shown)
    : m_config(config), m_job(std::move(job)), m_shown(std::move(shown)) {}

int JPPlacementsTableModel::columnOf(Col c) const {
    for (size_t i = 0; i < m_shown.size(); ++i)
        if (m_shown[i] == c) return int(i);
    return -1;
}

JPTableModel::Column JPPlacementsTableModel::column(int c) const {
    Column col;
    col.align = Align::Center;
    switch (m_shown[size_t(c)]) {
        case kEnabled:  col.name = "Enabled"; col.kind = Kind::Boolean; break;
        case kId:       col.name = "ID"; col.align = Align::Left; break;
        case kPart:     col.name = "Part"; col.kind = Kind::Choice; col.align = Align::Left; break;
        case kSide:     col.name = "Side"; col.kind = Kind::Choice; break;
        case kX:        col.name = "X"; col.decimalAligned = true; break;
        case kY:        col.name = "Y"; col.decimalAligned = true; break;
        case kRotation: col.name = "Rot."; col.decimalAligned = true; break;
        case kType:     col.name = "Type"; col.kind = Kind::Choice; break;
        case kPlaced:   col.name = "Placed"; col.kind = Kind::Boolean; break;
        case kStatus:   col.name = "Status"; break;
        case kErrorHandling: col.name = "Error Handling"; col.kind = Kind::Choice; break;
        case kRank:     col.name = "Rank"; col.kind = Kind::Number; break;
        case kComments: col.name = "Comments"; col.align = Align::Left; break;
        case kColumns:  break;
    }
    return col;
}

int JPPlacementsTableModel::rowCount() const {
    return m_holder ? int(m_holder->placements.size() + m_pseudo.size()) : 0;
}

void JPPlacementsTableModel::setLocation(JPPlacementsHolderLocation* location, bool editDefinition, JPJob* job) {
    m_location = location;
    m_editDefinition = editDefinition;
    m_placedJob = job;
    m_holder = location ? location->holder.get() : nullptr;
    reload();
}

bool JPPlacementsTableModel::rowShown(int row) const {
    if (!m_location) return true;
    const JPPlacement* p = placement(row);
    return p && p->side == m_location->globalSide();
}

JPPlacementsTableModel::Status JPPlacementsTableModel::status(const JPPlacement& p) const {
    const JPPart* part = m_config.part(p.partId);
    if (!part) return Status::MissingPart;
    if (!p.enabled) return Status::Disabled;
    if (p.type == JPPlacement::Type::Placement) {
        if (!hasFeeder || !hasFeeder(part->id)) return Status::MissingFeeder;
        if (part->isPartHeightUnknown()) return Status::ZeroPartHeight;
    }
    return Status::Ready;
}

const char* JPPlacementsTableModel::statusName(Status s) {
    switch (s) {
        case Status::Ready:          return "Ready";
        case Status::MissingPart:    return "Missing Part";
        case Status::MissingFeeder:  return "Missing Feeder";
        case Status::ZeroPartHeight: return "Part Height";
        case Status::Disabled:       return "Disabled";
    }
    return "";
}

void JPPlacementsTableModel::reload() {
    m_pseudo.clear();
    if (m_holder && m_holder->kind() == JPPlacementsHolder::Kind::Panel)
        m_pseudo = static_cast<JPPanel*>(m_holder)->pseudoPlacements();
}

bool JPPlacementsTableModel::isPseudo(int row) const {
    return m_holder && row >= int(m_holder->placements.size()) && row < rowCount();
}

JPPlacement* JPPlacementsTableModel::placement(int row) const {
    if (!m_holder || row < 0) return nullptr;
    const size_t own = m_holder->placements.size();
    if (size_t(row) < own) return &m_holder->placements[size_t(row)];
    if (size_t(row) - own < m_pseudo.size()) return const_cast<JPPlacement*>(&m_pseudo[size_t(row) - own]);
    return nullptr;
}

int JPPlacementsTableModel::rowOf(const std::string& id) const {
    for (int r = 0; r < rowCount(); ++r)
        if (placement(r)->id == id) return r;
    return -1;
}

std::string JPPlacementsTableModel::rowKey(int row) const {
    const JPPlacement* p = placement(row);
    return p ? p->id : std::string();
}

std::string JPPlacementsTableModel::text(int row, int c) const {
    const JPPlacement* p = placement(row);
    if (!p) return {};
    switch (m_shown[size_t(c)]) {
        case kId:   return p->id;
        case kPart: return m_config.part(p->partId) ? m_config.part(p->partId)->id : std::string();
        case kSide: return JPSides::name(p->side);
        case kX:    return JPLengthCell::text(p->location.lengthX(), true);
        case kY:    return JPLengthCell::text(p->location.lengthY(), true);
        case kRotation: return rotationText(p->location.rotation());
        case kType: return JPPlacement::typeName(p->type);
        case kErrorHandling: return JPPlacement::errorHandlingName(p->errorHandling);
        case kRank: return std::to_string(p->rank);
        case kComments: return p->comments.value_or("");
        case kStatus: return statusName(status(*p));
        default: return {};
    }
}

bool JPPlacementsTableModel::checked(int row, int c) const {
    const JPPlacement* p = placement(row);
    if (!p) return false;
    if (m_shown[size_t(c)] == kPlaced) return m_placedJob && m_location && m_placedJob->retrievePlacedStatus(*m_location, p->id);
    return m_shown[size_t(c)] == kEnabled && p->enabled;
}

double JPPlacementsTableModel::number(int row, int c) const {
    const JPPlacement* p = placement(row);
    return p && m_shown[size_t(c)] == kRank ? p->rank : 0;
}

int JPPlacementsTableModel::compareReferences(const std::string& s1, const std::string& s2) {
    const int n1 = firstDigit(s1), n2 = firstDigit(s2);
    if (n1 != -1 && n2 == -1) return 1;
    if (n1 == -1 && n2 != -1) return -1;
    if (n1 != -1 && n2 != -1) {
        const std::string f1 = s1.substr(0, size_t(n1)), f2 = s2.substr(0, size_t(n2));
        if (f1 != f2) return byteCompare(f1, f2);
        const long long a = digitsOf(s1.substr(size_t(n1))), b = digitsOf(s2.substr(size_t(n2)));
        return a < b ? -1 : a > b ? 1 : 0;
    }
    return byteCompare(s1, s2);
}

std::optional<int> JPPlacementsTableModel::compare(int a, int b, int c) const {
    const JPPlacement *x = placement(a), *y = placement(b);
    auto cmp = [](double u, double v) { return u < v ? -1 : u > v ? 1 : 0; };
    switch (m_shown[size_t(c)]) {
        case kId:       return compareReferences(x->id, y->id);
        case kSide:     return cmp(sideOrder(x->side), sideOrder(y->side));
        case kX:        return cmp(x->location.x(), y->location.x());
        case kY:        return cmp(x->location.y(), y->location.y());
        case kRotation: return cmp(x->location.rotation(), y->location.rotation());
        case kType:     return cmp(int(x->type), int(y->type));
        case kErrorHandling: return cmp(int(x->errorHandling), int(y->errorHandling));
        case kStatus:   return cmp(int(status(*x)), int(status(*y)));
        default:        return std::nullopt;
    }
}

std::string JPPlacementsTableModel::cellTooltip(int, int c) const {
    return m_shown[size_t(c)] == kRank ? kRankTooltip : std::string();
}

const uint8_t* JPPlacementsTableModel::cellTint(int row, int c) const {
    const JPPlacement* p = placement(row);
    if (p && m_shown[size_t(c)] == kType && p->type == JPPlacement::Type::Fiducial) return Colors::Accent;
    if (p && m_shown[size_t(c)] == kStatus) {
        switch (status(*p)) {
            case Status::Ready:          return Colors::Success;
            case Status::ZeroPartHeight: return Colors::Warning;
            case Status::Disabled:       return Colors::MutedText;
            default:                     return Colors::Danger;
        }
    }
    return nullptr;
}

bool JPPlacementsTableModel::editable(int row, int c) const {
    const Col col = m_shown[size_t(c)];
    if (isPseudo(row) || m_onlyEnabled) return col == kEnabled;
    // A job's: everything for a board used once straight in it, else its own on/off, placed and error handling.
    if (m_location && !m_editDefinition) return col == kEnabled || col == kPlaced || col == kErrorHandling;
    return col != kId && col != kStatus;
}

std::vector<const JPPart*> JPPlacementsTableModel::partChoices() const {
    std::vector<const JPPart*> out;
    for (const auto& p : m_config.parts()) out.push_back(p.get());
    std::sort(out.begin(), out.end(), [](const JPPart* a, const JPPart* b) { return a->id < b->id; });
    return out;
}

std::vector<std::string> JPPlacementsTableModel::choices(int, int c) const {
    std::vector<std::string> out;
    switch (m_shown[size_t(c)]) {
        case kPart:
            for (const JPPart* p : partChoices()) out.push_back(p->id);
            break;
        case kSide:
            for (JPSide s : kSides) out.push_back(JPSides::name(s));
            break;
        case kType:
            for (auto t : kTypes) out.push_back(JPPlacement::typeName(t));
            break;
        case kErrorHandling:
            for (auto e : kErrorHandlings) out.push_back(JPPlacement::errorHandlingName(e));
            break;
        default: break;
    }
    return out;
}

JPBoard* JPPlacementsTableModel::editedBoard() const {
    if (!m_holder) return nullptr;
    JPPlacementsHolder* def = m_location ? (m_editDefinition ? m_config.definitionOf(*m_holder) : nullptr) : m_holder;
    return def && def->kind() == JPPlacementsHolder::Kind::Board ? static_cast<JPBoard*>(def) : nullptr;
}

void JPPlacementsTableModel::edit(const std::string& id, const std::function<void(JPPlacement&)>& set) {
    if (!m_holder) return;
    if (m_location) {
        // A job's: the board itself when it may be, else this use of it alone.
        JPPlacementsHolder* def = m_editDefinition ? m_config.definitionOf(*m_holder) : nullptr;
        if (def) {
            JPDefinitionChanges(m_config, m_job()).placement(*def, id, set);
        } else if (JPPlacement* p = m_holder->find(id)) {
            set(*p);
        }
        if (m_placedJob) m_placedJob->dirty = true;
    } else {
        JPDefinitionChanges(m_config, m_job()).placement(*m_holder, id, set);
    }
    if (onChanged) onChanged();
}

void JPPlacementsTableModel::setPlaced(int row, bool placed) {
    const JPPlacement* p = placement(row);
    if (!p || !m_placedJob || !m_location) return;
    m_placedJob->storePlacedStatus(*m_location, p->id, placed);
    if (onChanged) onChanged();
}

bool JPPlacementsTableModel::setText(int row, int c, const std::string& text, std::string&) {
    const JPPlacement* p = placement(row);
    if (!p) return false;
    const std::string id = p->id;
    switch (m_shown[size_t(c)]) {
        case kX:
        case kY: {
            const auto length = JPLength::parse(text);
            if (!length) return false;
            const JPLocation l = p->location.withField(m_shown[size_t(c)] == kX ? JPLocation::Field::X : JPLocation::Field::Y,
                                                       *length, true);
            edit(id, [&l](JPPlacement& q) { q.location = l; });
            return true;
        }
        case kRotation: {
            const std::string t = text;
            char* end = nullptr;
            const double r = std::strtod(t.c_str(), &end);
            while (end && *end && std::isspace(uint8_t(*end))) ++end;
            if (t.empty() || !end || *end) return false;
            const JPLocation l = p->location.derive(std::nullopt, std::nullopt, std::nullopt, r);
            edit(id, [&l](JPPlacement& q) { q.location = l; });
            return true;
        }
        case kRank: {
            // As Java's Integer: an optional sign and digits, nothing else.
            size_t i = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
            if (i >= text.size()) return false;
            for (size_t j = i; j < text.size(); ++j)
                if (!std::isdigit(uint8_t(text[j]))) return false;
            const long long v = std::strtoll(text.c_str(), nullptr, 10);
            if (v < INT32_MIN || v > INT32_MAX) return false;
            edit(id, [v](JPPlacement& q) { q.rank = int(v); });
            return true;
        }
        case kComments:
            edit(id, [&text](JPPlacement& q) { q.comments = text; });
            return true;
        default: return false;
    }
}

void JPPlacementsTableModel::setChoice(int row, int c, int index) {
    const JPPlacement* p = placement(row);
    if (!p || index < 0) return;
    const std::string id = p->id;
    switch (m_shown[size_t(c)]) {
        case kPart: {
            const auto parts = partChoices();
            if (size_t(index) < parts.size()) {
                const std::string partId = parts[size_t(index)]->id;
                // A board's placement names one of the board's parts: that is what is matched to the part.
                if (JPBoard* board = editedBoard()) {
                    const std::string key = board->matchPlacement(id, partId);
                    edit(id, [&key, &partId](JPPlacement& q) {
                        q.boardPart = key;
                        q.partId = partId;
                    });
                    board->dropUnusedParts();
                } else {
                    edit(id, [&partId](JPPlacement& q) { q.partId = partId; });
                }
            }
            break;
        }
        case kSide:
            if (index < 2) edit(id, [s = kSides[index]](JPPlacement& q) { q.side = s; });
            break;
        case kType:
            if (index < 2) edit(id, [t = kTypes[index]](JPPlacement& q) { q.type = t; });
            break;
        case kErrorHandling:
            if (index < 3) edit(id, [e = kErrorHandlings[index]](JPPlacement& q) { q.errorHandling = e; });
            break;
        default: break;
    }
}

void JPPlacementsTableModel::setChecked(int row, int c, bool on) {
    const JPPlacement* p = placement(row);
    if (p && m_shown[size_t(c)] == kPlaced) {
        setPlaced(row, on);
        return;
    }
    if (p && isPseudo(row) && m_shown[size_t(c)] == kEnabled) {
        auto* panel = static_cast<JPPanel*>(m_holder);
        if (on) panel->disabledPseudoPlacements.erase(p->id);
        else panel->disabledPseudoPlacements.insert(p->id);
        reload();
        if (onChanged) onChanged();
        return;
    }
    if (p && m_shown[size_t(c)] == kEnabled) edit(p->id, [on](JPPlacement& q) { q.enabled = on; });
}

} // inline namespace jf
