// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"

#include <j/core/JComboBox.h>

#include <string>
#include <vector>

inline namespace jf {

// How a choice of the library's parts or packages is searched as it is made
// (JComboBox::Search): the Parts and Packages tabs' Show beside the filter
// (All, Used by open boards, each open board; Used by open boards first when
// the job uses any), found as JPPartMatcher::searchRank and
// packageSearchRank find; of parts equally good, one no feeder holds yet
// comes before one a feeder does. The table it drops is the page's
// (JPChooserTable, named by kPartsTable and kPackagesTable).
class JPLibrarySearch {
public:
    // Show's first two choices (the Parts and Packages tabs' too).
    static constexpr const char* kShowAll = "All";
    static constexpr const char* kShowUsed = "Used by open boards";
    // The tables a search drops, as a form names them (JPFormBuilder::searchable).
    static constexpr const char* kPartsTable = "parts";
    static constexpr const char* kPackagesTable = "packages";

    // `ids`: the choice's entries in order, "" for none.
    static JComboBox::Search parts(const JPConfiguration& config, const std::vector<std::string>& ids);
    static JComboBox::Search packages(const JPConfiguration& config, const std::vector<std::string>& ids);
    // What a search made here shows besides its entries: the library's parts, the open boards and which
    // part each uses. A form with a search made before this changed is made again.
    static std::string sources(const JPConfiguration& config);
};

} // inline namespace jf
