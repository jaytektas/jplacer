// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPKicadBoardFile.h"

#include "model/JPKicadModImporter.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

inline namespace jf {

namespace {

// An s-expression: a list (its items, and where its text lies in the file) or an atom.
struct Node {
    bool              list = false;
    std::string       atom;
    std::vector<Node> items;
    size_t            begin = 0, end = 0;

    const std::string& head() const {
        static const std::string none;
        return list && !items.empty() && !items[0].list ? items[0].atom : none;
    }
    // Its first list item headed `name`, else null.
    const Node* child(const std::string& name) const {
        for (const Node& n : items)
            if (n.head() == name) return &n;
        return nullptr;
    }
    // Its i-th item's atom, else empty.
    const std::string& at(size_t i) const {
        static const std::string none;
        return i < items.size() && !items[i].list ? items[i].atom : none;
    }
    bool has(const std::string& atom) const {
        for (const Node& n : items)
            if (!n.list && n.atom == atom) return true;
        return false;
    }
};

class Reader {
public:
    explicit Reader(const std::string& text) : m_text(text) {}

    Node read() {
        skip();
        if (m_at >= m_text.size() || m_text[m_at] != '(') throw std::runtime_error("not a KiCad board file (no s-expression)");
        return list();
    }

private:
    const std::string& m_text;
    size_t             m_at = 0;

    void skip() {
        while (m_at < m_text.size() && std::isspace(uint8_t(m_text[m_at]))) ++m_at;
    }
    Node list() {
        Node n;
        n.list = true;
        n.begin = m_at++;
        for (;;) {
            skip();
            if (m_at >= m_text.size()) throw std::runtime_error("the file ends inside an expression");
            const char c = m_text[m_at];
            if (c == ')') {
                n.end = ++m_at;
                return n;
            }
            if (c == '(') {
                n.items.push_back(list());
            } else if (c == '"') {
                n.items.push_back(quoted());
            } else {
                Node a;
                const size_t from = m_at;
                while (m_at < m_text.size() && !std::isspace(uint8_t(m_text[m_at])) && m_text[m_at] != '(' && m_text[m_at] != ')')
                    ++m_at;
                a.atom = m_text.substr(from, m_at - from);
                n.items.push_back(std::move(a));
            }
        }
    }
    Node quoted() {
        Node a;
        for (++m_at; m_at < m_text.size() && m_text[m_at] != '"'; ++m_at) {
            if (m_text[m_at] == '\\' && m_at + 1 < m_text.size()) ++m_at;
            a.atom += m_text[m_at];
        }
        if (m_at >= m_text.size()) throw std::runtime_error("the file ends inside a quoted string");
        ++m_at;
        return a;
    }
};

double number(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

// A number as KiCad's .pos writes one: up to four places, no trailing zeros, no "-0".
std::string text(double v) {
    if (std::fabs(v) < 5e-7) v = 0;
    char b[48];
    std::snprintf(b, sizeof b, "%.6f", v);
    std::string s = b;
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

// An angle in [0, 360), as a library's footprint has its pads.
double degrees(double a) {
    a = std::fmod(a, 360.0);
    if (a < 0) a += 360;
    if (a > 360 - 1e-9 || std::fabs(a) < 1e-9) a = 0;
    return a;
}

// A footprint's text field: KiCad 6 on's (property "Reference" "R1"), KiCad 5's (fp_text reference R1).
std::string field(const Node& fp, const std::string& property, const std::string& fpText) {
    for (const Node& n : fp.items) {
        if (n.head() == "property" && n.at(1) == property) return n.at(2);
        if (n.head() == "fp_text" && n.at(1) == fpText) return n.at(2);
    }
    return {};
}

bool samePads(const JPFootprint& a, const JPFootprint& b) {
    if (a.pads.size() != b.pads.size()) return false;
    auto near = [](double x, double y) { return std::fabs(x - y) < 1e-6; };
    for (size_t i = 0; i < a.pads.size(); ++i) {
        const JPFootprint::Pad &p = a.pads[i], &q = b.pads[i];
        if (p.name != q.name || !near(p.x, q.x) || !near(p.y, q.y) || !near(p.width, q.width) || !near(p.height, q.height)
            || !near(p.rotation, q.rotation) || !near(p.roundness, q.roundness))
            return false;
    }
    return true;
}

} // namespace

bool JPKicadBoardFile::is(const std::string& path) {
    const std::string end = ".kicad_pcb";
    if (path.size() < end.size()) return false;
    for (size_t i = 0; i < end.size(); ++i)
        if (std::tolower(uint8_t(path[path.size() - end.size() + i])) != end[i]) return false;
    return true;
}

bool JPKicadBoardFile::read(const std::string& path, JPImportSource& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "Cannot read " + path;
        return false;
    }
    const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!parse(bytes, out, error)) return false;
    out.table.path = path;
    return true;
}

bool JPKicadBoardFile::parse(const std::string& source, JPImportSource& out, std::string& error) {
    Node board;
    try {
        board = Reader(source).read();
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    if (board.head() != "kicad_pcb") {
        error = "Not a KiCad board file: it begins (" + board.head() + " where a board begins (kicad_pcb";
        return false;
    }
    // Positions from the drill/place file origin (KiCad's own "use drill/place file origin"), Y up.
    double ox = 0, oy = 0;
    if (const Node* setup = board.child("setup"))
        if (const Node* o = setup->child("aux_axis_origin")) {
            ox = number(o->at(1));
            oy = number(o->at(2));
        }

    JPTableFile& t = out.table;
    t = JPTableFile();
    t.separator = ' ';
    t.headerLine = 0;
    t.header = { "Ref", "Val", "Footprint", "PosX", "PosY", "Rot", "Side", "DNP" };
    t.comments = { "Unit = mm, Angle = deg." };
    out.footprints.clear();
    out.notes.clear();
    std::map<std::string, std::vector<std::string>> variants;   // a footprint name → the names its ways are kept by
    std::map<std::string, std::string>               firstOf;   // a kept name → the first placement drawn so

    for (const Node& fp : board.items) {
        if (fp.head() != "footprint" && fp.head() != "module") continue;
        const Node* attr = fp.child("attr");
        if (attr && (attr->has("exclude_from_pos_files") || attr->has("virtual"))) continue;   // KiCad leaves these out
        const std::string ref = field(fp, "Reference", "reference"), value = field(fp, "Value", "value");
        std::string name = fp.at(1);
        if (const size_t colon = name.find(':'); colon != std::string::npos) name = name.substr(colon + 1);
        const Node* layer = fp.child("layer");
        const bool bottom = layer && layer->at(1) == "B.Cu";
        const Node* at = fp.child("at");
        const double x = at ? number(at->at(1)) : 0, y = at ? number(at->at(2)) : 0, rot = at ? number(at->at(3)) : 0;
        t.rows.push_back({ ref, value, name, text(x - ox), text(oy - y), text(rot), bottom ? "bottom" : "top",
                           attr && attr->has("dnp") ? "yes" : "" });

        // Its pads as its library draws them: the placement's rotation taken off each pad's (KiCad writes a
        // pad's angle on the board), and a bottom one's mirror (KiCad mirrors its pads across X) undone.
        JPFootprint drawn;
        drawn.pads = JPKicadModImporter::parse(source.substr(fp.begin, fp.end - fp.begin), bottom ? "B.Cu" : "F.Cu");
        for (JPFootprint::Pad& p : drawn.pads) {
            const double local = p.rotation - rot;
            p.rotation = degrees(bottom ? -local : local);
            if (bottom) p.y = -p.y;
            if (std::fabs(p.y) < 1e-12) p.y = 0;
        }
        std::vector<std::string>& ways = variants[name];
        std::string kept;
        for (const std::string& w : ways)
            if (samePads(out.footprints[w], drawn)) kept = w;
        if (kept.empty()) {
            kept = ways.empty() ? name : name + " (" + std::to_string(ways.size() + 1) + ")";
            ways.push_back(kept);
            out.footprints[kept] = std::move(drawn);
            firstOf[kept] = ref;
            if (ways.size() > 1)
                out.notes.push_back(name + " is drawn " + std::to_string(ways.size()) + " ways on the board: " + ref
                                    + "'s pads differ from " + firstOf[name] + "'s, so it and those drawn as it is take "
                                    + kept);
        }
        t.rows.back()[2] = kept;
    }
    if (t.rows.empty()) {
        error = "No footprints on the board for a position file";
        return false;
    }
    return true;
}

} // inline namespace jf
