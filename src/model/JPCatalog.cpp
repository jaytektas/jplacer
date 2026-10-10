// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCatalog.h"

#include "JPPartMatcher.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>

inline namespace jf {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// A board's id without the board's name before it ("grblhal/R_0603" → "R_0603").
std::string unscoped(const JPBoard& board, const std::string& id) {
    const std::string prefix = board.scopeName() + "/";
    return id.rfind(prefix, 0) == 0 ? id.substr(prefix.size()) : id;
}

// A kind's word and the standard footprint names it stands for (KiCad's libraries, IPC-7351).
struct Kind {
    const char* word;
    std::vector<const char*> names;
};
const std::vector<Kind>& kinds() {
    static const std::vector<Kind> k { { "cap", { "c_", "cp_", "capacitor" } }, { "res", { "r_", "resistor" } },
                                       { "ind", { "l_", "inductor" } },         { "led", { "led" } },
                                       { "fid", { "fiducial" } } };
    return k;
}

// How well one word is found in `fields` (lower-cased): at the start of one, or of a piece of one after
// "_", "-", ":" or a space, 3; inside one, 1; not at all, 0.
int found(const std::string& word, const std::vector<std::string>& fields) {
    int best = 0;
    for (const std::string& f : fields) {
        for (size_t at = f.find(word); at != std::string::npos; at = f.find(word, at + 1)) {
            const bool start = at == 0 || std::strchr("_-: /", f[at - 1]);
            best = std::max(best, start ? 3 : 1);
            if (best == 3) return best;
        }
    }
    return best;
}

int score(const std::vector<std::string>& fields, const std::string& value, const std::string& words) {
    const std::vector<std::string> ws = JPPartMatcher::words(words);
    if (ws.empty()) return 1;
    double have = 0;
    const bool valued = !value.empty() && JPPartMatcher::valueOf(value, have);
    int total = 0;
    for (const std::string& raw : ws) {
        const std::string w = lower(raw);
        int s = found(w, fields);
        // A value written another way: 100n is 0.1u is 100nF (a word with a digit in it, so "cap" is not one).
        double want = 0;
        if (valued && std::any_of(w.begin(), w.end(), [](char c) { return std::isdigit(uint8_t(c)); })
            && JPPartMatcher::valueOf(w, want) && std::fabs(want - have) <= 1e-9 * std::max(std::fabs(want), std::fabs(have)))
            s = std::max(s, 3);
        for (const Kind& k : kinds())
            if (w == k.word)
                for (const char* n : k.names) s = std::max(s, found(n, fields) == 3 ? 2 : 0);
        if (s == 0) return 0;
        total += s;
    }
    return total;
}

} // namespace

std::vector<JPCatalog::Part> JPCatalog::parts(const JPConfiguration& config) {
    std::vector<Part> out;
    for (const auto& p : config.parts()) out.push_back({ p, nullptr, "", Status::Library, p->id });
    for (const auto& b : config.boards())
        for (const JPBoardPart& bp : b->parts()) {
            Part e;
            e.board = b;
            e.boardPartKey = bp.key;
            switch (bp.state) {
                case JPBoardPart::State::Local:
                    e.held = bp.localPart;
                    e.status = Status::Own;
                    break;
                case JPBoardPart::State::Matched:
                    e.held = bp.copyPart;
                    if (!e.held)
                        for (const auto& lp : config.parts())
                            if (lp.get() == config.libraryPartFor(bp)) e.held = lp;
                    e.status = config.differs(bp) ? Status::LibraryChanged : Status::Matched;
                    break;
                case JPBoardPart::State::Unmatched:
                    e.status = Status::ToBeChosen;
                    break;
            }
            e.name = e.held ? unscoped(*b, e.held->id) : bp.field("part");
            out.push_back(std::move(e));
        }
    return out;
}

std::vector<JPCatalog::Package> JPCatalog::packages(const JPConfiguration& config) {
    std::vector<Package> out;
    for (const auto& k : config.packages()) out.push_back({ k, nullptr, "", Status::Library, k->id });
    for (const auto& b : config.boards()) {
        std::set<std::pair<std::string, bool>> seen;   // id, own
        for (const JPBoardPart& bp : b->parts()) {
            std::shared_ptr<JPPackage> k;
            Status s = Status::Own;
            if (bp.state == JPBoardPart::State::Local && bp.localPackage) {
                k = bp.localPackage;
            } else if (bp.state == JPBoardPart::State::Matched && bp.copyPackage) {
                k = bp.copyPackage;
                s = config.differs(bp) ? Status::LibraryChanged : Status::Matched;
            }
            if (!k || !seen.insert({ k->id, s == Status::Own }).second) continue;
            const std::string name = unscoped(*b, k->id);
            out.push_back({ std::move(k), b, bp.key, s, name });
        }
    }
    return out;
}

const char* JPCatalog::statusName(Status s) {
    switch (s) {
        case Status::Library:        return "";
        case Status::Matched:        return "Matched";
        case Status::LibraryChanged: return "Library changed";
        case Status::Own:            return "Own";
        case Status::ToBeChosen:     return "To be chosen";
    }
    return "";
}

const char* JPCatalog::statusTip(Status s) {
    switch (s) {
        case Status::Library:        return "The library's own";
        case Status::Matched:        return "The board's copy of the library's, as the library has it (change the library's)";
        case Status::LibraryChanged:
            return "The board's copy of the library's; the library's has changed since (right-click: Update from Library)";
        case Status::Own:            return "The board's own: the library has none (right-click: Add to Library)";
        case Status::ToBeChosen:     return "Not chosen yet: only what the board's files said (choose it on the Boards tab)";
    }
    return "";
}

std::string JPCatalog::source(const JPBoard* board) {
    return board ? board->scopeName() : std::string("Library");
}

bool JPCatalog::addToLibrary(JPConfiguration& config, const Part& entry, std::string& error) {
    const JPBoardPart* bp = entry.boardPart();
    if (entry.status != Status::Own || !entry.held || !bp) {
        error = "Only a board's own part is added to the library";
        return false;
    }
    if (config.libraryPart(entry.name)) {
        error = "The library has a part called " + entry.name + " already";
        return false;
    }
    auto part = std::make_shared<JPPart>(*entry.held);
    part->id = entry.name;
    part->uuid.clear();
    if (const JPPackage* own = bp->localPackage.get()) {
        const std::string id = unscoped(*entry.board, own->id);
        if (!config.libraryPackage(id)) {
            auto k = std::make_shared<JPPackage>(*own);
            k->id = id;
            k->uuid.clear();
            config.addPackage(std::move(k));
        }
        part->packageId = id;
    }
    config.addPart(std::move(part));
    return true;
}

void JPCatalog::updateFromLibrary(const JPConfiguration& config, const Part& entry) {
    JPBoardPart* bp = entry.boardPart();
    if (entry.status != Status::LibraryChanged || !bp) return;
    config.takeCopy(*bp);
    entry.board->dirty = true;
}

void JPCatalog::shareEdit(JPBoard& board, const JPPackage& edited) {
    for (JPBoardPart& bp : board.parts())
        if (bp.localPackage && bp.localPackage.get() != &edited && bp.localPackage->id == edited.id) *bp.localPackage = edited;
}

int JPCatalog::matches(const JPConfiguration& config, const Part& entry, const std::string& words) {
    std::vector<std::string> fields { lower(entry.name) };
    std::string value;
    if (const JPPart* p = entry.part()) {
        value = p->value;
        fields.push_back(lower(p->id));
        fields.push_back(lower(p->value));
        fields.push_back(lower(p->name.value_or("")));
        fields.push_back(lower(p->packageId));
        if (const JPPackage* k = config.package(p->packageId)) fields.push_back(lower(k->description.value_or("")));
        for (const auto& i : p->identifiers) fields.push_back(lower(i.code));
        for (const auto& a : p->akas) fields.push_back(lower(a.text));
    }
    if (const JPBoardPart* bp = entry.boardPart())
        for (const auto& [k, v] : bp->fields) {
            fields.push_back(lower(v));
            if (k == "value" && value.empty()) value = v;
        }
    return score(fields, value, words);
}

int JPCatalog::matches(const Package& entry, const std::string& words) {
    std::vector<std::string> fields { lower(entry.name) };
    if (entry.held) fields.push_back(lower(entry.held->description.value_or("")));
    return score(fields, "", words);
}

} // inline namespace jf
