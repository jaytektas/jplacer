// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLengthUnit.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// One field of what a table's rows are (DESIGN.md, Fields): a part's Height, a placement's Rank, or one added
// (an LCSC part number, a supplier, a revision). Every table's fields are of one kind (JPFieldSet); a built-in
// one is jplacer's own (its value is the thing's own, its column the table's), an added one keeps its values
// with each thing (JPFieldValues) by its key. Any field can be renamed and given the headings an import's
// column matches it by; one that is not required can be removed; an added one has a type its values are
// read and shown by:
//
//   Text     as typed                          Number   a number ("12", "-0.5")
//   Length   in millimetres, typed in any of   Yes/No   a tick
//            jplacer's units ("1.2", "40mil")  Choice   one of its choices
class JPField {
public:
    enum class Type { Text, Number, Length, YesNo, Choice };

    std::string              key;        // for good: a built-in's own ("height"); one added "u1", "u2", …
    std::string              name;       // as shown, unique among its kind's fields
    Type                     type = Type::Text;
    std::vector<std::string> choices;    // a Choice's, in order
    // The headings an import's column is matched to it by (compared as JPImportField::normalise compares them):
    // an added field's those it was made from or given; a built-in's those jplacer knows and any given.
    std::vector<std::string> matches;
    bool        builtIn = false;
    bool        required = false;   // jplacer cannot work without it: never removed
    bool        removed = false;    // a built-in removed: in no table, no import, no search (Restore brings it back)
    // A built-in that an import fills: the import's own name for what it reads (JPImportField::key, "designator");
    // empty: none fills it. An added field is always one an import may fill.
    std::string importAs;
    std::string about;              // what a built-in is, said where it is chosen

    static const std::vector<Type>& types();
    static const char* typeKey(Type t);     // in files: "text", "number", "length", "yesNo", "choice"
    static const char* typeLabel(Type t);   // as offered: "Text", "Number", "Length", "Yes/No", "Choice"
    static Type        typeFrom(const std::string& key);

    // Whether an import may fill it: an added one, or a built-in an import reads.
    bool imported() const { return !builtIn || !importAs.empty(); }
    // A value as text: a number as short as it reads, a length in millimetres, Yes/No as "Yes" or empty; none
    // (null): empty.
    std::string text(const JJson& value) const;
    // Text as a value of its type (a length in any units, `unitless` where it names none, kept in
    // millimetres; Yes/No from "yes", "true", "1", "y", "x" and their opposites; a choice by its name, without
    // regard to case). Empty text is no value (null). False, and why, when the text is not one of its type.
    bool parse(const std::string& text, JJson& value, std::string& error,
               JPLengthUnit unitless = JPLengthUnit::Millimeters) const;
    // Whether an import's column heading is one of its matches.
    bool matchesHeading(const std::string& heading) const;

    JJson toJson() const;
    static JPField fromJson(const JJson& j);
};

} // inline namespace jf
