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
// merged into the chosen one. Parts and packages it makes are added to
// `config` as it reads, as OpenPnP's do.
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

    // A part by id, else one made with a package of `packageId` (made too
    // when there is none), as OpenPnP's importers make them.
    static JPPart* findOrMakePart(JPConfiguration& config, const std::string& partId, const std::string& packageId);
};

} // inline namespace jf
