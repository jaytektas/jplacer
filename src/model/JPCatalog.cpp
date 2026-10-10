// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCatalog.h"

#include <algorithm>
#include <cctype>
#include <map>

inline namespace jf {

std::vector<JPCatalog::Part> JPCatalog::parts(const JPConfiguration& config) {
    std::vector<Part> out;
    std::map<const JPPart*, size_t> at;
    for (const auto& p : config.parts()) {
        at[p.get()] = out.size();
        out.push_back({ p, {} });
    }
    for (const auto& b : config.boards())
        for (const JPBoardPart& bp : b->parts()) {
            if (bp.state != JPBoardPart::State::Matched) continue;
            const auto it = at.find(config.libraryPartFor(bp));
            if (it == at.end()) continue;
            auto& by = out[it->second].usedBy;
            if (std::find(by.begin(), by.end(), b) == by.end()) by.push_back(b);
        }
    return out;
}

std::vector<JPCatalog::Package> JPCatalog::packages(const JPConfiguration& config) {
    std::vector<Package> out;
    std::map<std::string, size_t> at;   // by id, upper case
    auto upper = [](std::string s) {
        for (char& c : s) c = char(std::toupper(uint8_t(c)));
        return s;
    };
    for (const auto& k : config.packages()) {
        at[upper(k->id)] = out.size();
        out.push_back({ k, {} });
    }
    for (const auto& b : config.boards())
        for (const JPBoardPart& bp : b->parts()) {
            const JPPart* p = bp.state == JPBoardPart::State::Matched ? config.libraryPartFor(bp) : nullptr;
            if (!p) continue;
            const auto it = at.find(upper(p->packageId));
            if (it == at.end()) continue;
            auto& by = out[it->second].usedBy;
            if (std::find(by.begin(), by.end(), b) == by.end()) by.push_back(b);
        }
    return out;
}

std::string JPCatalog::boardNames(const std::vector<std::shared_ptr<JPBoard>>& boards) {
    std::string s;
    for (const auto& b : boards) s += (s.empty() ? "" : ", ") + b->scopeName();
    return s;
}

bool JPCatalog::uses(const std::vector<std::shared_ptr<JPBoard>>& boards, const std::string& name) {
    return std::any_of(boards.begin(), boards.end(), [&name](const auto& b) { return b->scopeName() == name; });
}

} // inline namespace jf
