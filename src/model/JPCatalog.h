// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"

#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// The library's parts and packages (the only ones there are), each with the
// open boards that use it: a board's matched parts (and their packages).
// What the Parts and Packages tabs list.
class JPCatalog {
public:
    // Each holds what it shows and the boards it names, so a row stays good while they change under it (an
    // import adding to a board), until the views read the rows again.
    struct Part {
        std::shared_ptr<JPPart>               held;
        std::vector<std::shared_ptr<JPBoard>> usedBy;   // the open boards whose parts are it
        JPPart* part() const { return held.get(); }
    };
    struct Package {
        std::shared_ptr<JPPackage>            held;
        std::vector<std::shared_ptr<JPBoard>> usedBy;   // the open boards with a part of it
        JPPackage* package() const { return held.get(); }
    };

    // The library's, in its order.
    static std::vector<Part>    parts(const JPConfiguration& config);
    static std::vector<Package> packages(const JPConfiguration& config);

    // The boards' names, "grblhal, sim"; empty for none.
    static std::string boardNames(const std::vector<std::shared_ptr<JPBoard>>& boards);
    // Whether `boards` holds one named `name` (JPBoard::scopeName).
    static bool uses(const std::vector<std::shared_ptr<JPBoard>>& boards, const std::string& name);

};

} // inline namespace jf
