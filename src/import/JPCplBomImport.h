// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPImportSource.h"

#include "model/JPBoard.h"
#include "model/JPConfiguration.h"

#include <map>
#include <string>
#include <vector>

inline namespace jf {

// A board made from its CAD files (DESIGN.md, Import): the placement file
// (CPL, sources[0]) and any BOMs and other tables, joined on the designator
// (a BOM line's "R1, R2, R5-R8" expanded). Each placement from the CPL; each
// field of its part from the file chosen to win for that field (the CPL for
// a placement's, else the first other file that has it), another file's
// where that one says nothing. What does not fit is reported, not guessed:
// designators in one file and not the other, fields the files disagree on.
// The board's parts grouped by manufacturer and MPN where given, else by
// value and footprint; every field each was given kept with it; matched to
// the library's part named by its MPN, its footprint-value, or its value,
// else (Create Missing Parts) to a part made for the library (out.madeParts,
// put there as the import is taken), else not chosen yet. A package made for
// one (the library has none of that name) has the footprint the placement
// file draws, where it is a board file that does (a KiCad .kicad_pcb).
// The fields added (JPFields) that columns fill: a placement's from its
// row; a part's and its package's from its rows (kept with the board part,
// "part:<key>" and "package:<key>", given to the library's part and package
// as the import is taken: JPConfiguration::takeMade), what the rows of one
// part disagree on noted; the board's and the job's one value for the whole
// import: a column's (the first row's, what the others disagree on noted),
// else what a file says of itself (about) by the field's name or matches,
// else what is typed for it (typed, over either).
class JPCplBomImport {
public:
    struct Conflict {
        JPImportField::Target                     field;
        std::string                               designator;
        std::vector<std::pair<int, std::string>>  values;   // source index, what it says
    };
    struct Report {
        std::vector<std::string> cplOnly;    // placed, named by none of the other files (when there are any)
        std::vector<std::string> otherOnly;  // named by another file, not in the placement file
        std::vector<Conflict>    conflicts;
        std::vector<std::string> problems;   // rows passed over and why; files that cannot be joined
        int placements = 0, doNotPlace = 0, fiducials = 0, noPart = 0, parts = 0, matched = 0, made = 0, unmatched = 0;
        int footprints = 0;   // the board file's footprints the packages made for the library take
        // What the fields added were not given as they are: a value its type cannot read, a part's rows or the
        // import's that disagree (the first taken), a library part's or package's value the files differ from
        // (the library's kept).
        std::vector<std::string> fieldNotes;
        // The job's values: for the job the board goes into (those it has no value in yet).
        JPFieldValues job;
    };

    std::vector<JPImportSource>          sources;   // [0]: the placement file
    std::map<std::string, int>           winners;   // a field's (Target::text) source, where chosen; else the default
    bool                                 createMissing = false;
    // A board's or the job's field's value as typed (by its key, the text as typed), over what the files say.
    std::map<std::string, std::string>   typedBoard, typedJob;

    // The source whose value of `field` is taken first.
    int winner(const JPImportField::Target& field) const;
    int winner(JPImportField::Id field) const { return winner(JPImportField::target(field)); }
    // A board's or the job's field's value for the whole import, as the files give it (a column's first, else
    // what a file says of itself), not what is typed; empty: none.
    std::string single(const JPFields& fields, const JPImportField::Target& field) const;
    // The join alone: what the report would say, nothing built.
    Report check() const;
    // The board: placements, parts, provenance (as of `when`). False (`error`) when the placement file
    // cannot make one (no designator, X or Y column).
    bool build(const JPConfiguration& config, const std::string& when, JPBoard& out, Report& report,
               std::string& error) const;

    // Whether a cell of a do-not-place column (its `header`) says the part is not placed: "DNP", "yes" in a
    // DNP column; "no" in a Populate / Fitted / Mount one.
    static bool doNotPlace(const std::string& header, const std::string& cell);
    // Whether a side cell says the bottom ("Bottom", "BottomLayer", "B", "B.Cu", a mirror "Yes").
    static bool bottom(const std::string& cell);

private:
    // Each designator's row in each source (-1: none), the CPL's first.
    std::map<std::string, std::vector<int>> join(Report& report) const;
    // A field of a designator: the winner's, else any other source's; every source's differing value noted.
    std::string value(const std::map<std::string, std::vector<int>>& rows, const std::string& designator,
                      const JPImportField::Target& field, Report* report) const;
    std::string value(const std::map<std::string, std::vector<int>>& rows, const std::string& designator,
                      JPImportField::Id field, Report* report) const {
        return value(rows, designator, JPImportField::target(field), report);
    }
};

} // inline namespace jf
