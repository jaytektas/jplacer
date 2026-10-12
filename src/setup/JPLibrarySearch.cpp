// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLibrarySearch.h"

#include "model/JPCatalog.h"
#include "model/JPPartMatcher.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

inline namespace jf {

namespace {

// A search's coarse filter, as the Parts and Packages tabs' Show: All, Used by open boards, each open
// board, over `ids`, each entry used by the boards `boards` names; Used by open boards first when the job
// uses any. An entry past `ids` (the value a choice has now, not in its list) and none ("") are in every one.
void scopes(JComboBox::Search& s, const JPConfiguration& config, const std::vector<std::string>& ids,
            std::vector<std::vector<std::string>> boards) {
    const bool anyInJob = std::any_of(boards.begin(), boards.end(), [](const auto& b) { return !b.empty(); });
    s.scopesLabel = "Show";
    s.scopes = { JPLibrarySearch::kShowAll, JPLibrarySearch::kShowUsed };
    for (const auto& b : config.boards()) s.scopes.push_back(b->scopeName());
    s.scope = anyInJob ? 1 : 0;
    s.inScope = [boards = std::move(boards), ids, names = s.scopes](int scope, int i) {
        if (scope == 0 || size_t(i) >= ids.size() || ids[size_t(i)].empty()) return true;
        const std::vector<std::string>& by = boards[size_t(i)];
        if (scope == 1) return !by.empty();
        return std::find(by.begin(), by.end(), names[size_t(scope)]) != by.end();
    };
}

// What a catalogue entry's thing is known by for good (a part's and a package's id, a GUID).
const std::string& keyOfHeld(const JPPart& p) { return p.id; }
const std::string& keyOfHeld(const JPPackage& k) { return k.id; }

// Each entry's boards' names, from the catalogue's (JPCatalog) uses by id.
template <class Entries>
std::vector<std::vector<std::string>> boardsOf(const Entries& entries, const std::vector<std::string>& ids) {
    std::map<std::string, std::vector<std::string>> by;
    for (const auto& e : entries)
        for (const auto& b : e.usedBy) by[keyOfHeld(*e.held)].push_back(b->scopeName());
    std::vector<std::vector<std::string>> out;
    for (const std::string& id : ids) {
        const auto at = by.find(id);
        out.push_back(at == by.end() ? std::vector<std::string>() : at->second);
    }
    return out;
}

bool nothingTyped(const std::string& typed) { return typed.find_first_not_of(" \t") == std::string::npos; }

} // namespace

JComboBox::Search JPLibrarySearch::parts(const JPConfiguration& config, const std::vector<std::string>& ids) {
    std::vector<std::vector<std::string>> boards = boardsOf(JPCatalog::parts(config), ids);
    std::set<std::string> fed;
    for (const JPFeeder& f : config.feeders())
        if (!f.partId().empty()) fed.insert(f.partId());

    JComboBox::Search s;
    s.placeholder = "Filter: id, value, package, MPN (\"lq\", \"0603 100n\", \"cap\")";
    auto held = std::make_shared<std::vector<bool>>();
    for (const std::string& id : ids) held->push_back(fed.count(id) > 0);
    scopes(s, config, ids, std::move(boards));
    s.rank = [&config, ids, held](int i, const std::string& typed) {
        if (size_t(i) >= ids.size()) return 0;
        if (ids[size_t(i)].empty()) return nothingTyped(typed) ? 0 : -1;   // none: while nothing is typed
        const JPPart* p = config.part(ids[size_t(i)]);
        const int r = p ? JPPartMatcher::searchRank(config, *p, typed) : -1;
        return r < 0 ? r : 2 * r + ((*held)[size_t(i)] ? 1 : 0);
    };
    return s;
}

JComboBox::Search JPLibrarySearch::packages(const JPConfiguration& config, const std::vector<std::string>& ids) {
    JComboBox::Search s;
    s.placeholder = "Filter: name, description (\"0603\", \"qfp\", \"cap\")";
    scopes(s, config, ids, boardsOf(JPCatalog::packages(config), ids));
    s.rank = [&config, ids](int i, const std::string& typed) {
        if (size_t(i) >= ids.size()) return 0;
        if (ids[size_t(i)].empty()) return nothingTyped(typed) ? 0 : -1;
        const JPPackage* k = config.package(ids[size_t(i)]);
        return k ? JPPartMatcher::packageSearchRank(*k, typed) : -1;
    };
    return s;
}

std::string JPLibrarySearch::sources(const JPConfiguration& config) {
    std::string out = std::to_string(config.parts().size()) + "/" + std::to_string(config.packages().size());
    for (const auto& b : config.boards()) out += "|" + b->scopeName();
    for (const JPCatalog::Part& e : JPCatalog::parts(config))
        if (!e.usedBy.empty()) out += "|" + e.part()->id + ":" + JPCatalog::boardNames(e.usedBy);
    return out;
}

} // inline namespace jf
