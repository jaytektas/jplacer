// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCplImporter.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

inline namespace jf {

namespace {

constexpr double kMilPerMm  = 1000.0 / 25.4;
constexpr double kInchPerMm = 1.0 / 25.4;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// One CSV record: fields split on `sep`, double quotes around a field (with
// "" for a quote inside).
std::vector<std::string> splitRecord(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
            else if (c == '"') quoted = false;
            else field += c;
        } else if (c == '"') {
            quoted = true;
        } else if (c == sep) {
            out.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    out.push_back(trim(field));
    return out;
}

// What each column is, from its heading: the names PCB tools use.
enum class Column { Designator, X, Y, Rotation, Side, Footprint, Value, Other };

struct Heading {
    Column column = Column::Other;
    double unitsPerMm = 0;   // from "(mm)", "(mil)" in the heading; 0: not said
    int    rank = 0;         // among several for one column, the higher is used
};

Heading classify(const std::string& raw) {
    std::string h = lower(trim(raw));
    Heading out;
    if (h.find("(mil)") != std::string::npos || h.find("[mil]") != std::string::npos) out.unitsPerMm = kMilPerMm;
    if (h.find("(mm)") != std::string::npos || h.find("[mm]") != std::string::npos) out.unitsPerMm = 1;
    if (h.find("(in)") != std::string::npos || h.find("(inch)") != std::string::npos) out.unitsPerMm = kInchPerMm;
    if (const size_t p = h.find_first_of("(["); p != std::string::npos) h = trim(h.substr(0, p));
    auto is = [&h](std::initializer_list<const char*> names) {
        return std::any_of(names.begin(), names.end(), [&h](const char* n) { return h == n; });
    };
    // A part's centre is what a placer wants; a footprint's reference point or
    // pad 1 is a second choice.
    if (is({ "designator", "ref", "refdes", "reference", "ref des", "part", "name" }))
        out.column = Column::Designator, out.rank = h == "name" ? 1 : 2;
    else if (is({ "mid x", "center-x", "center x", "centerx", "posx", "pos x", "x" }))
        out.column = Column::X, out.rank = 3;
    else if (is({ "mid y", "center-y", "center y", "centery", "posy", "pos y", "y" }))
        out.column = Column::Y, out.rank = 3;
    else if (is({ "ref x", "ref-x", "refx" }))
        out.column = Column::X, out.rank = 2;
    else if (is({ "ref y", "ref-y", "refy" }))
        out.column = Column::Y, out.rank = 2;
    else if (is({ "pad x", "pad-x" }))
        out.column = Column::X, out.rank = 1;
    else if (is({ "pad y", "pad-y" }))
        out.column = Column::Y, out.rank = 1;
    else if (is({ "rotation", "rot", "angle", "orientation" }))
        out.column = Column::Rotation, out.rank = 1;
    else if (is({ "layer", "side", "tb", "top/bottom" }))
        out.column = Column::Side, out.rank = 1;
    else if (is({ "footprint", "package", "pattern" }))
        out.column = Column::Footprint, out.rank = h == "footprint" ? 2 : 1;
    else if (is({ "value", "val", "comment" }))
        out.column = Column::Value, out.rank = h == "comment" ? 1 : 2;
    return out;
}

// A length: a number with an optional unit after it ("12.5mm", "500 mil").
bool length(const std::string& text, double headingUnitsPerMm, double& mm) {
    const std::string t = trim(text);
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) return false;
    const std::string unit = lower(trim(std::string(end)));
    double perMm = headingUnitsPerMm > 0 ? headingUnitsPerMm : 1;
    if (unit == "mm") perMm = 1;
    else if (unit == "mil") perMm = kMilPerMm;
    else if (unit == "in" || unit == "inch" || unit == "\"") perMm = kInchPerMm;
    else if (!unit.empty()) return false;
    mm = v / perMm;
    return true;
}

bool sideOf(const std::string& text, JPPlacement::Side& side) {
    const std::string t = lower(trim(text));
    if (t == "t" || t == "top" || t == "f.cu" || t == "toplayer" || t == "f") { side = JPPlacement::Side::Top; return true; }
    if (t == "b" || t == "bottom" || t == "b.cu" || t == "bottomlayer") { side = JPPlacement::Side::Bottom; return true; }
    return false;
}

bool startsWith(const std::string& s, const char* prefix) {
    return lower(s).rfind(prefix, 0) == 0;
}

} // namespace

bool JPCplImporter::read(const std::string& path, JPBoard& board, std::vector<std::string>& notes, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    if (!parse(ss.str(), board, notes, error)) {
        error = path + ": " + error;
        return false;
    }
    if (board.name.empty()) board.name = std::filesystem::path(path).stem().string();
    JLOGC(JPlacerLog::kImportCpl, JLogLevel::Info) << path << ": " << board.placements.size() << " placement(s)";
    return true;
}

bool JPCplImporter::parse(const std::string& textIn, JPBoard& board, std::vector<std::string>& notes, std::string& error) {
    std::string text = textIn;
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);   // UTF-8 byte-order mark
    std::vector<std::string> lines;
    {
        std::string line;
        std::istringstream is(text);
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!trim(line).empty()) lines.push_back(line);
        }
    }
    // The heading row: the first that names a designator and both coordinates,
    // with whichever separator makes it so (tools write comma, semicolon or tab).
    size_t headRow = lines.size();
    char sep = ',';
    std::vector<Heading> heads;
    for (size_t i = 0; i < lines.size() && headRow == lines.size(); ++i)
        for (const char s : { ',', ';', '\t' }) {
            std::vector<Heading> h;
            for (const std::string& f : splitRecord(lines[i], s)) h.push_back(classify(f));
            auto has = [&h](Column c) { return std::any_of(h.begin(), h.end(), [c](const Heading& x) { return x.column == c; }); };
            if (has(Column::Designator) && has(Column::X) && has(Column::Y)) {
                headRow = i;
                sep = s;
                heads = std::move(h);
                break;
            }
        }
    if (headRow == lines.size()) {
        error = "no heading row naming a designator and X and Y columns";
        return false;
    }
    int col[7];
    std::fill(std::begin(col), std::end(col), -1);
    for (size_t c = 0; c < heads.size(); ++c) {
        const int k = int(heads[c].column);
        if (heads[c].column == Column::Other) continue;
        if (col[k] < 0 || heads[c].rank > heads[size_t(col[k])].rank) col[k] = int(c);
    }
    auto field = [&](const std::vector<std::string>& rec, Column c) -> std::string {
        const int i = col[int(c)];
        return i >= 0 && size_t(i) < rec.size() ? rec[size_t(i)] : std::string();
    };
    if (col[int(Column::Side)] < 0) notes.push_back("no side column: every placement is taken to be on the top");
    if (col[int(Column::Rotation)] < 0) notes.push_back("no rotation column: every placement is taken to be unturned");

    int skipped = 0;
    for (size_t i = headRow + 1; i < lines.size(); ++i) {
        const std::vector<std::string> rec = splitRecord(lines[i], sep);
        JPPlacement p;
        p.designator = field(rec, Column::Designator);
        const double xUnits = heads[size_t(col[int(Column::X)])].unitsPerMm;
        const double yUnits = heads[size_t(col[int(Column::Y)])].unitsPerMm;
        if (p.designator.empty() || !length(field(rec, Column::X), xUnits, p.x) || !length(field(rec, Column::Y), yUnits, p.y)) {
            ++skipped;
            continue;
        }
        if (const std::string r = field(rec, Column::Rotation); !r.empty()) p.rotationDeg = std::strtod(r.c_str(), nullptr);
        if (const std::string s = field(rec, Column::Side); !s.empty() && !sideOf(s, p.side)) {
            notes.push_back(p.designator + ": side '" + s + "' not understood; taken as top");
        }
        p.footprint = field(rec, Column::Footprint);
        p.value     = field(rec, Column::Value);
        p.fiducial  = startsWith(p.designator, "fid") || lower(p.footprint).find("fiducial") != std::string::npos;
        board.placements.push_back(std::move(p));
    }
    if (skipped) notes.push_back(std::to_string(skipped) + " row(s) without a designator and position were left out");
    if (board.placements.empty()) {
        error = "no placements";
        return false;
    }
    return true;
}

} // inline namespace jf
