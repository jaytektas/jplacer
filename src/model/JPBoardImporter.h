// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoard.h"
#include "JPConfiguration.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

inline namespace jf {

// One of OpenPnP's board importers (Boards ▸ Import Placements, File ▸
// Import Board): its name in the menus, its description (the entry's
// tooltip and its dialog's title), the files its dialog asks for and the
// options it ticks; and the reading, into a board whose placements are then
// merged into the chosen one. The parts it names become the board's own
// parts (JPBoardPart), matched to the library's where it has them; unlike
// OpenPnP's, it adds nothing to the library.
class JPBoardImporter {
public:
    struct File {
        std::string              label;        // "Top File (.pos)"
        std::vector<std::string> extensions;   // what its Browse shows
    };
    struct Option {
        std::string label;
        std::string tooltip;
        bool        initial = false;
    };

    virtual ~JPBoardImporter() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual std::vector<File>   files() const = 0;
    virtual std::vector<Option> options() const = 0;

    // `files` as typed in the dialog (one that is not there is passed over,
    // as OpenPnP does), `options` as ticked. False, and why in OpenPnP's
    // words, when a file cannot be read.
    bool read(const std::vector<std::string>& files, const std::vector<bool>& options, JPConfiguration& config,
              JPBoard& out, std::string& error) const;

    // What its dialog shows (titled "Import Error") when a reading fails:
    // the reason, or some importers' own explanation of their format.
    virtual std::string failureText(const std::string& error) const { return error; }

    // OpenPnP's importers, in its menus' order.
    static std::vector<std::unique_ptr<JPBoardImporter>> all();

protected:
    // What a reading stops on: the message OpenPnP's would show.
    struct Failure : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    virtual void parse(const std::vector<std::string>& files, const std::vector<bool>& options, JPConfiguration& config,
                       JPBoard& out) const = 0;

    static bool exists(const std::string& path);
    // A file's lines, as Java reads them (\n, \r or \r\n ending one); the
    // text taken as UTF-8, or Latin-1 (`latin1`) made UTF-8.
    static std::vector<std::string> lines(const std::string& path, bool latin1 = false);
    // A file's text as UTF-8: UTF-16 when it opens with FF FE (the mark
    // dropped), else Latin-1, as OpenPnP's CSV importer reads one.
    static std::string csvText(const std::string& path);
    static std::vector<std::string> splitLines(const std::string& text);
    // Java's String.trim, split (trailing empty fields dropped) and
    // Double.parseDouble, failing as they do.
    static std::string trim(const std::string& s);
    static std::vector<std::string> split(const std::string& s, char separator);
    static std::vector<std::string> splitWhitespace(const std::string& s);
    static double number(const std::string& s);
    static const std::string& at(const std::vector<std::string>& fields, size_t i);
    static char first(const std::string& s);
    static std::string upper(std::string s);

    // The board part for a part the file names (`partId`, its footprint `packageId`, its `value` where the
    // file says), in `out`: one already made for it, else made, matched to the library's part of that id
    // where there is one; else, `create` (Create Missing Parts), the board's own part, with the library's
    // package of that id or (none) one of its own; else unmatched. Never added to the library: a board's
    // parts are the board's. Its key; `part` (given) the part its placements use, null when unmatched;
    // `made` whether this call made the board's own part.
    static std::string boardPart(JPConfiguration& config, JPBoard& out, const std::string& partId,
                                 const std::string& packageId, const std::string& value, bool create,
                                 JPPart** part = nullptr, bool* made = nullptr);
    // A placement given its board part (`key`, from boardPart).
    static void assign(const JPBoard& out, JPPlacement& p, const std::string& key);
};

} // inline namespace jf
