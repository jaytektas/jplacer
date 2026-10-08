// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardUpgrade.h"

#include "JPAngles.h"

#include "common/JPUuid.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>

inline namespace jf {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMovedMm = 0.01;       // further than this, the CAD moved it (files give 0.001 mm or coarser)
constexpr double kTurnedDeg = 0.05;
constexpr double kRenumberMm = 0.05;    // a renumbered placement is found within this of where it was
constexpr double kOriginMm = 0.02;      // the placements the board's own move explains are within this of it
constexpr size_t kOriginPairs = 3;      // fewer paired by designator: no move is told from them

struct Mm {
    double x, y;
};

Mm mm(const JPPlacement& p) {
    const JPLocation l = p.location.convertToUnits(JPLengthUnit::Millimeters);
    return { l.x(), l.y() };
}

double cad(const JPPlacement& p) { return p.cadRotation.value_or(p.location.rotation()); }

std::string number(const char* format, double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, format, v);
    return buf;
}

} // namespace

JPBoardUpgrade::JPBoardUpgrade(JPBoard& board, const JPBoard& files) : m_board(board), m_files(files) {
    board.giveIdentities();
    std::set<std::string> reused;
    for (const JPBoardPart& np : files.parts())
        for (const JPBoardPart& op : board.parts())
            if (!reused.count(op.key) && op.samePart(np)) {
                m_same[np.key] = &op;
                reused.insert(op.key);
                break;
            }
    // By designator.
    std::map<std::string, const JPPlacement*> boardById;
    for (const JPPlacement& p : board.placements) boardById[p.id] = &p;
    std::set<const JPPlacement*> paired;
    std::vector<const JPPlacement*> newOnly;
    for (const JPPlacement& n : files.placements) {
        const auto w = boardById.find(n.id);
        if (w != boardById.end() && !paired.count(w->second)) {
            m_pairs.push_back({ w->second, &n });
            paired.insert(w->second);
        } else {
            newOnly.push_back(&n);
        }
    }
    findOriginMove(m_pairs);
    // The rest by footprint and position: renumbered.
    auto footprint = [](const JPBoard& b, const JPPlacement& p) {
        const JPBoardPart* bp = b.part(p.boardPart);
        return bp && !bp->footprintName().empty() ? bp->footprintName() : p.partId;
    };
    for (const JPPlacement* n : newOnly) {
        const JPPlacement moved = inBoard(*n);
        const Mm at = mm(moved);
        const JPPlacement* best = nullptr;
        double bestD = kRenumberMm;
        for (const JPPlacement& w : board.placements) {
            if (paired.count(&w) || w.side != n->side || footprint(board, w) != footprint(files, *n)) continue;
            const Mm was = mm(w);
            const double d = std::hypot(was.x - at.x, was.y - at.y);
            if (d <= bestD) {
                bestD = d;
                best = &w;
            }
        }
        if (best) paired.insert(best);
        m_pairs.push_back({ best, n });
    }
    for (const JPPlacement& w : board.placements)
        if (!paired.count(&w)) m_pairs.push_back({ &w, nullptr });
    for (Pair& p : m_pairs) {
        p.row = m_rows.size();
        m_rows.emplace_back();
        sort(p);
    }
}

void JPBoardUpgrade::findOriginMove(const std::vector<Pair>& byDesignator) {
    if (byDesignator.size() < kOriginPairs) return;
    // The turn most of them share (to a tenth of a degree), then the offset most share at that turn.
    std::map<long long, size_t> turns;
    for (const Pair& p : byDesignator) ++turns[std::llround(JPAngles::difference(cad(*p.was), cad(*p.now)) * 10)];
    long long turnKey = 0;
    size_t most = 0;
    for (const auto& [k, n] : turns)
        if (n > most) {
            most = n;
            turnKey = k;
        }
    const double turn = double(turnKey) / 10;
    const double c = std::cos(turn * kPi / 180), s = std::sin(turn * kPi / 180);
    auto offset = [&](const Pair& p) {
        const Mm w = mm(*p.was), n = mm(*p.now);
        return Mm { n.x - (c * w.x - s * w.y), n.y - (s * w.x + c * w.y) };
    };
    std::map<std::pair<long long, long long>, size_t> offsets;
    for (const Pair& p : byDesignator) {
        const Mm o = offset(p);
        ++offsets[{ std::llround(o.x * 100), std::llround(o.y * 100) }];
    }
    std::pair<long long, long long> offsetKey {};
    most = 0;
    for (const auto& [k, n] : offsets)
        if (n > most) {
            most = n;
            offsetKey = k;
        }
    const Mm mode { double(offsetKey.first) / 100, double(offsetKey.second) / 100 };
    // The ones it explains, averaged: the move to the files' precision.
    double sx = 0, sy = 0;
    size_t n = 0;
    for (const Pair& p : byDesignator) {
        const Mm o = offset(p);
        if (std::hypot(o.x - mode.x, o.y - mode.y) <= kOriginMm
            && std::abs(JPAngles::difference(cad(*p.was), cad(*p.now)) - turn) <= kTurnedDeg) {
            sx += o.x;
            sy += o.y;
            ++n;
        }
    }
    if (n * 2 <= byDesignator.size()) return;
    const double dx = sx / double(n), dy = sy / double(n);
    if (std::hypot(dx, dy) <= kMovedMm && std::abs(turn) <= kTurnedDeg) return;
    m_originMoved = true;
    m_dx = dx;
    m_dy = dy;
    m_turn = turn;
}

JPPlacement JPBoardUpgrade::inBoard(const JPPlacement& p) const {
    if (!m_originMoved) return p;
    JPPlacement q = p;
    const JPLocation l = p.location.convertToUnits(JPLengthUnit::Millimeters);
    const double c = std::cos(-m_turn * kPi / 180), s = std::sin(-m_turn * kPi / 180);
    const double x = l.x() - m_dx, y = l.y() - m_dy;
    q.location = JPLocation(JPLengthUnit::Millimeters, c * x - s * y, s * x + c * y, l.z(),
                            JPAngles::normalise(l.rotation() - m_turn))
                     .convertToUnits(p.location.units());
    if (q.cadRotation) q.cadRotation = JPAngles::normalise(*q.cadRotation - m_turn);
    return q;
}

void JPBoardUpgrade::sort(Pair& p) {
    Row& r = m_rows[p.row];
    if (p.was) r.was = p.was->id;
    if (p.now) r.now = p.now->id;
    const JPBoardPart* np = p.now ? m_files.part(p.now->boardPart) : nullptr;
    if (np) {
        const auto same = m_same.find(np->key);
        r.toMatch = (same != m_same.end() ? same->second->state : np->state) == JPBoardPart::State::Unmatched;
    }
    if (!p.was || !p.now) return;
    std::vector<std::string> said;
    if (r.was != r.now) said.push_back("was " + r.was);
    const JPPlacement now = inBoard(*p.now);
    const Mm w = mm(*p.was), n = mm(now);
    const double d = std::hypot(n.x - w.x, n.y - w.y);
    r.moved = d > kMovedMm;
    if (r.moved) said.push_back("moved " + number("%.2f", d) + " mm");
    const double turn = JPAngles::difference(cad(*p.was), cad(now));
    r.turned = std::abs(turn) > kTurnedDeg;
    if (r.turned) said.push_back("turned " + number("%g", turn) + "°");
    const JPBoardPart* wp = m_board.part(p.was->boardPart);
    if (wp && np) {
        r.footprintChanged = wp->footprintName() != np->footprintName();
        r.partChanged = !r.footprintChanged && !wp->samePart(*np);
        if (r.footprintChanged) said.push_back("footprint " + wp->footprintName() + " → " + np->footprintName());
        if (r.partChanged) {
            auto name = [](const JPBoardPart& bp) {
                for (const char* f : { "value", "mpn", "part", "supplierPn", "manufacturer" })
                    if (!bp.field(f).empty()) return bp.field(f);
                return std::string();
            };
            std::string was = name(*wp), now = name(*np);
            // Two the same by the first field each has: the one that differs named.
            for (const char* f : { "value", "mpn", "part", "supplierPn", "manufacturer" })
                if (wp->field(f) != np->field(f)) {
                    was = wp->field(f);
                    now = np->field(f);
                    break;
                }
            said.push_back("part " + (was.empty() ? "-" : was) + " → " + (now.empty() ? "-" : now));
        }
    } else {
        r.partChanged = p.was->partId != p.now->partId;
        if (r.partChanged) said.push_back("part " + p.was->partId + " → " + p.now->partId);
    }
    for (const std::string& s : said) r.detail += (r.detail.empty() ? "" : ", ") + s;
}

bool JPBoardUpgrade::is(const Row& r, Kind k) {
    const bool both = !r.was.empty() && !r.now.empty();
    switch (k) {
        case Kind::Unchanged:
            return both && r.was == r.now && !r.moved && !r.turned && !r.partChanged && !r.footprintChanged;
        case Kind::Moved:            return r.moved || r.turned;
        case Kind::PartChanged:      return r.partChanged;
        case Kind::FootprintChanged: return r.footprintChanged;
        case Kind::New:              return r.was.empty();
        case Kind::Renamed:          return both && r.was != r.now;
        case Kind::Removed:          return r.now.empty();
    }
    return false;
}

size_t JPBoardUpgrade::count(Kind k) const {
    size_t n = 0;
    for (const Row& r : m_rows) n += is(r, k);
    return n;
}

std::string JPBoardUpgrade::words(Kind k, size_t n) {
    const std::string c = std::to_string(n);
    switch (k) {
        case Kind::Unchanged:        return c + " unchanged";
        case Kind::Moved:            return c + " moved (to verify)";
        case Kind::PartChanged:      return c + (n == 1 ? " part changed" : " parts changed");
        case Kind::FootprintChanged: return c + (n == 1 ? " footprint changed" : " footprints changed") + " (to verify)";
        case Kind::New:              return c + " new";
        case Kind::Renamed:          return c + " renamed";
        case Kind::Removed:          return c + " removed";
    }
    return c;
}

std::string JPBoardUpgrade::summary() const {
    std::string s;
    if (m_originMoved) {
        s = "Origin moved " + number("%.2f", m_dx) + ", " + number("%.2f", m_dy) + " mm";
        if (std::abs(m_turn) > kTurnedDeg) s += ", turned " + number("%g", m_turn) + "°";
        s += "; ";
    }
    std::string counts;
    for (Kind k : kKinds)
        if (const size_t n = count(k); n > 0 || k == Kind::Unchanged) counts += (counts.empty() ? "" : ", ") + words(k, n);
    size_t toMatch = 0;
    for (const Row& r : m_rows) toMatch += r.toMatch && !r.now.empty();
    if (toMatch > 0) counts += "; " + std::to_string(toMatch) + " placement(s) with parts to choose";
    return s + counts;
}

JPBoardRevision JPBoardUpgrade::revision(const std::string& label, const std::string& when) const {
    JPBoardRevision rev;
    rev.label = label;
    rev.made = when;
    rev.summary = summary();
    rev.provenance = m_files.provenance;
    // Board parts follow their lines of the files: one the same keeps its key and its choice (what the files
    // say now kept); another is the import's, under a key no other has.
    std::set<std::string> taken;
    for (const JPBoardPart& p : m_board.parts()) taken.insert(p.key);
    std::map<std::string, std::string> keys;
    int next = 1;
    for (const JPBoardPart& np : m_files.parts()) {
        JPBoardPart q;
        if (const auto same = m_same.find(np.key); same != m_same.end()) {
            q = *same->second;
            q.takeChoice(*same->second);
            q.fields = np.fields;
        } else {
            q = np;
            m_board.scopeOwn(q);
            while (taken.count("bp-" + std::to_string(next))) ++next;
            q.key = "bp-" + std::to_string(next);
            taken.insert(q.key);
        }
        keys[np.key] = q.key;
        rev.parts.push_back(q);
    }
    for (const Pair& p : m_pairs) {
        if (!p.now) continue;
        const Row& r = m_rows[p.row];
        JPPlacement q = inBoard(*p.now);
        if (const auto k = keys.find(q.boardPart); k != keys.end()) q.boardPart = k->second;
        if (!p.was) {
            q.uid = JPUuid::make();
            rev.placements.push_back(q);
            continue;
        }
        const JPPlacement& w = *p.was;
        q.uid = w.uid;
        q.enabled = w.enabled;
        q.errorHandling = w.errorHandling;
        q.rank = w.rank;
        if (!q.comments) q.comments = w.comments;
        // The correction made on the machine carried as a difference from the CAD; a new footprint's zero
        // rotation is the import's.
        if (!r.footprintChanged && w.cadRotation && q.cadRotation) {
            const double correction = JPAngles::difference(*w.cadRotation, w.location.rotation());
            q.location = q.location.derive(std::nullopt, std::nullopt, std::nullopt,
                                           JPAngles::normalise(*q.cadRotation + correction));
        }
        if (!r.moved && !r.turned && !r.footprintChanged) q.verified = w.verified;
        rev.placements.push_back(q);
    }
    return rev;
}

} // inline namespace jf
