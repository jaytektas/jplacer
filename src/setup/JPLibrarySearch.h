// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"

#include <j/core/JComboBox.h>

#include <string>
#include <vector>

inline namespace jf {

// How a choice of the library's parts or packages is searched as it is made
// (JComboBox::Search): beside each its value and package (a part's) or its
// description (a package's), and the open boards that use it; All, In the
// Job (what the open boards use) and each open board to look in, In the Job
// first when the job uses any; found as JPPartMatcher::searchRank and
// packageSearchRank find. Of parts equally good, one no feeder holds yet
// comes before one a feeder does.
class JPLibrarySearch {
public:
    static constexpr const char* kAll = "All";
    static constexpr const char* kInTheJob = "In the Job";

    // `ids`: the choice's entries in order, "" for none.
    static JComboBox::Search parts(const JPConfiguration& config, const std::vector<std::string>& ids);
    static JComboBox::Search packages(const JPConfiguration& config, const std::vector<std::string>& ids);
    // What a search made here shows besides its entries: the library's parts, the open boards and which
    // part each uses. A form with a search made before this changed is made again.
    static std::string sources(const JPConfiguration& config);
};

} // inline namespace jf
