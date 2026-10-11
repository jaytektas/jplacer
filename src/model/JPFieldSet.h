// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPField.h"

#include <j/config/Json.h>

#include <string>
#include <vector>

inline namespace jf {

// The fields of one kind of thing (DESIGN.md, Fields): every table of parts shows the parts' fields, every
// table of placements the placements' (a board's, a job's, a panel's fiducials), and an import fills them.
// Its built-in fields are jplacer's own, in its order (those its tables show, and those an import reads that
// no table shows: a part's Footprint and Quantity); those added follow, in the order they were added, each
// with a key of its own never given again.
class JPFieldSet {
public:
    enum class Of { Parts, Packages, Pads, Placements, Locations, Boards, Panels, Feeders, Vision, Job };
    static const std::vector<Of>& kinds();
    static const char* key(Of of);      // in files: "parts", "placements", …
    static const char* label(Of of);    // one of them, as a field's kind is said: "Part", "Placement", "Board", …
    static const char* plural(Of of);   // "Parts", "Placements", …

    explicit JPFieldSet(Of of);

    Of of() const { return m_of; }
    // Every field: the built-ins (those removed too) and those added.
    const std::vector<JPField>& fields() const { return m_fields; }
    // The fields there are (none removed), in order.
    std::vector<const JPField*> present() const;
    // The fields added (none removed: an added one removed is gone), in order.
    std::vector<const JPField*> added() const;
    // A field by its key (a built-in removed too), or null.
    const JPField* field(const std::string& key) const;
    // A field there is, by its name without regard to case, or null.
    const JPField* named(const std::string& name) const;
    // Whether `key` is a field there is (a built-in removed is not).
    bool has(const std::string& key) const;
    // Whether a built-in has a name of its own (renamed): a table shows it by that, not its own heading.
    bool renamed(const std::string& key) const;

    // Whether `name` may be a field's (the field `key`'s, empty: a new one's): not empty, and no other field
    // there is has it; why not, when it may not.
    bool nameFree(const std::string& name, const std::string& key, std::string& error) const;
    // A field added (its key given here), after the others: its key. Its name must be free (nameFree).
    std::string add(JPField f);
    // A field's name, type, choices and matches as `f` has them (a built-in's name and matches only).
    // False, and why, when its name is taken.
    bool update(const JPField& f, std::string& error);
    // A field taken out: an added one gone (its values the caller's to take out), a built-in removed until
    // restored. A required one stays (false).
    bool remove(const std::string& key);
    // A built-in removed, back.
    void restore(const std::string& key);

    // As kept: those added, and the built-ins changed (renamed, given matches, removed).
    JJson toJson() const;
    // Read back over the built-ins.
    void fromJson(const JJson& j);

private:
    Of                   m_of;
    std::vector<JPField> m_fields;
    std::vector<JPField> m_builtIns;   // as jplacer has them, for what toJson keeps
    int                  m_next = 1;   // the next added field's number ("u1")
};

} // inline namespace jf
