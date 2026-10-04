// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPKicadFootprint.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

inline namespace jf {

namespace {

// An S-expression: an atom, or a list whose first item names it.
struct Node {
    std::string       atom;
    std::vector<Node> items;
    bool              list = false;

    const std::string& head() const {
        static const std::string none;
        return list && !items.empty() && !items[0].list ? items[0].atom : none;
    }
    const Node* child(const std::string& name) const {
        for (const Node& n : items)
            if (n.head() == name) return &n;
        return nullptr;
    }
    double number(size_t i) const { return i < items.size() ? std::strtod(items[i].atom.c_str(), nullptr) : 0; }
    std::string text(size_t i) const { return i < items.size() ? items[i].atom : std::string(); }
};

class Parser {
public:
    explicit Parser(const std::string& t) : m_t(t) {}

    bool parse(Node& out, std::string& error) {
        skip();
        if (m_p >= m_t.size() || m_t[m_p] != '(') {
            error = "not a KiCad footprint (no opening parenthesis)";
            return false;
        }
        if (!list(out, 0)) {
            error = "not a KiCad footprint (unbalanced parentheses)";
            return false;
        }
        return true;
    }

private:
    void skip() {
        while (m_p < m_t.size() && std::isspace(static_cast<unsigned char>(m_t[m_p]))) ++m_p;
    }
    bool list(Node& n, int depth) {
        if (depth > 64) return false;
        n.list = true;
        ++m_p;   // (
        for (;;) {
            skip();
            if (m_p >= m_t.size()) return false;
            const char c = m_t[m_p];
            if (c == ')') {
                ++m_p;
                return true;
            }
            Node item;
            if (c == '(') {
                if (!list(item, depth + 1)) return false;
            } else if (c == '"') {
                ++m_p;
                while (m_p < m_t.size() && m_t[m_p] != '"') {
                    if (m_t[m_p] == '\\' && m_p + 1 < m_t.size()) ++m_p;
                    item.atom += m_t[m_p++];
                }
                ++m_p;
            } else {
                while (m_p < m_t.size() && !std::isspace(static_cast<unsigned char>(m_t[m_p])) && m_t[m_p] != '('
                       && m_t[m_p] != ')')
                    item.atom += m_t[m_p++];
            }
            n.items.push_back(std::move(item));
        }
    }

    const std::string& m_t;
    size_t             m_p = 0;
};

bool onLayer(const Node& n, const std::string& layer) {
    const Node* l = n.child("layer");
    return l && l->text(1) == layer;
}

} // namespace

bool JPKicadFootprint::parse(const std::string& text, JPFootprint& out, std::string& error) {
    Node root;
    Parser p(text);
    if (!p.parse(root, error)) return false;
    if (root.head() != "footprint" && root.head() != "module") {
        error = "not a KiCad footprint (it is a '" + root.head() + "')";
        return false;
    }
    JPFootprint f;
    f.name = root.text(1);
    double minX = std::numeric_limits<double>::max(), minY = minX, maxX = -minX, maxY = -minX;
    auto grow = [&](double x, double y) {
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    };
    for (const Node& n : root.items) {
        const std::string& h = n.head();
        if ((h == "fp_line" || h == "fp_rect") && onLayer(n, "F.Fab")) {
            for (const char* end : { "start", "end" })
                if (const Node* e = n.child(end)) grow(e->number(1), -e->number(2));
        } else if (h == "pad") {
            const std::string number = n.text(1);
            const std::string type = n.text(2);
            const std::string shape = n.text(3);
            if (type == "np_thru_hole" || number.empty()) continue;
            const Node* at = n.child("at");
            const Node* size = n.child("size");
            if (!at || !size) continue;
            JPPad pad;
            pad.name = number;
            pad.x = at->number(1);
            pad.y = -at->number(2);
            pad.rotationDeg = at->number(3);
            pad.width = size->number(1);
            pad.height = size->number(2);
            if (shape == "circle" || shape == "oval") pad.roundness = 1;
            else if (shape == "roundrect")
                if (const Node* r = n.child("roundrect_rratio")) pad.roundness = std::min(1.0, r->number(1) * 2);
            f.pads.push_back(std::move(pad));
        }
    }
    if (f.pads.empty()) {
        error = "the footprint has no pads";
        return false;
    }
    if (maxX > minX && maxY > minY) {
        f.bodyWidth = maxX - minX;
        f.bodyLength = maxY - minY;
    }
    if (f.pad("1")) f.pin1 = "1";
    else if (f.pad("A1")) f.pin1 = "A1";
    out = std::move(f);
    return true;
}

bool JPKicadFootprint::read(const std::string& path, JPFootprint& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    if (!parse(ss.str(), out, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

} // inline namespace jf
