// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLibrarySearch.h"

#include "model/JPCatalog.h"
#include "model/JPLengthUnits.h"
#include "model/JPPartMatcher.h"

#include <algorithm>
#include <cstdio>
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

// Each entry's boards' names, from the catalogue's (JPCatalog) uses by id.
template <class Entries>
std::vector<std::vector<std::string>> boardsOf(const Entries& entries, const std::vector<std::string>& ids) {
    std::map<std::string, std::vector<std::string>> by;
    for (const auto& e : entries)
        for (const auto& b : e.usedBy) by[e.held->id].push_back(b->scopeName());
    std::vector<std::vector<std::string>> out;
    for (const std::string& id : ids) {
        const auto at = by.find(id);
        out.push_back(at == by.end() ? std::vector<std::string>() : at->second);
    }
    return out;
}

std::string joined(const std::vector<std::string>& names) {
    std::string s;
    for (const std::string& n : names) s += (s.empty() ? "" : ", ") + n;
    return s;
}

bool nothingTyped(const std::string& typed) { return typed.find_first_not_of(" \t") == std::string::npos; }

} // namespace

JComboBox::Search JPLibrarySearch::parts(const JPConfiguration& config, const std::vector<std::string>& ids) {
    std::vector<std::vector<std::string>> boards = boardsOf(JPCatalog::parts(config), ids);
    std::map<std::string, int> feeders;
    for (const JPFeeder& f : config.feeders())
        if (!f.partId().empty()) ++feeders[f.partId()];

    JComboBox::Search s;
    s.placeholder = "Filter: id, value, package, MPN (\"lq\", \"0603 100n\", \"cap\")";
    // The long description last, so it does not push the others out of sight.
    s.columns = { "ID", "Value", "Package", "Height", "MPN", "Used By", "Feeders", "Description" };
    auto held = std::make_shared<std::vector<bool>>();
    for (size_t i = 0; i < ids.size(); ++i) {
        const JPPart* p = ids[i].empty() ? nullptr : config.part(ids[i]);
        const auto fed = feeders.find(ids[i]);
        const int holding = fed == feeders.end() ? 0 : fed->second;
        if (!p) {
            s.cells.push_back({ ids[i].empty() ? "(none)" : ids[i], "", "", "", "", joined(boards[i]), "", "" });
        } else {
            std::string height;
            if (!p->isPartHeightUnknown()) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%.2f %s", p->height.value(), JPLengthUnits::shortName(p->height.units()));
                height = buf;
            }
            s.cells.push_back({ p->id, p->value, p->packageId, height,
                                p->identifiers.empty() ? std::string() : p->identifiers.front().code, joined(boards[i]),
                                holding ? std::to_string(holding) : std::string(), p->name.value_or("") });
        }
        held->push_back(holding > 0);
    }
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
    std::vector<std::vector<std::string>> boards = boardsOf(JPCatalog::packages(config), ids);
    JComboBox::Search s;
    s.placeholder = "Filter: id, description (\"0603\", \"qfp\", \"cap\")";
    s.columns = { "ID", "Description", "Used By" };
    for (size_t i = 0; i < ids.size(); ++i) {
        const JPPackage* k = ids[i].empty() ? nullptr : config.package(ids[i]);
        s.cells.push_back({ ids[i].empty() ? "(none)" : ids[i], k ? k->description.value_or("") : std::string(),
                            joined(boards[i]) });
    }
    scopes(s, config, ids, std::move(boards));
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
