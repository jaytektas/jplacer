// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPTable.h"

#include "JPOpenPnpIcons.h"

#include <j/core/JArrow.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/graphics/VectorGraphics.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

inline namespace jf {

namespace {

// As Swing's TableRowSorter: three sort keys at most.
constexpr size_t kMostSortKeys = 3;
// The second and third sort keys' marks fade by this much each (OpenPnP's
// MultisortTableHeaderCellRenderer).
constexpr float kSortKeyFade = 0.5f;
// A row's tick box, as a share of the row.
constexpr float kTickShare = 0.6f;
// Icons are drawn at twice their size and shown smaller, to stay sharp.
constexpr float kIconOversample = 2.0f;
// A tinted cell's fill: its colour, this opaque over the row.
constexpr float kHighlightAlpha = 0.5f;
// The widest number a decimal-aligned column lines up (OpenPnP's "%9.3f"
// and two places for units), split at its point.
constexpr const char* kAlignedWhole = "-00000";
constexpr const char* kAlignedRest  = ".000mm";

JColor colour(const uint8_t* c) { return rgb(c[0], c[1], c[2]); }

// As a collator orders text: by the letters first, without regard to case,
// then lower case before upper.
int collate(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int ca = std::tolower(static_cast<unsigned char>(a[i])), cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return std::islower(static_cast<unsigned char>(a[i])) ? -1 : 1;
    return 0;
}

// Text cut to `width`, ending in "..." where it is cut, as Swing cuts a cell.
std::string elided(const std::string& t, float width) {
    if (JTextHelper::measureWidth(t) <= width) return t;
    const std::string dots = "...";
    size_t lo = 0, hi = t.size();
    while (lo < hi) {   // the longest start that fits with the dots
        size_t mid = (lo + hi + 1) / 2;
        while (mid > 0 && mid < t.size() && (static_cast<unsigned char>(t[mid]) & 0xC0) == 0x80) --mid;
        if (mid <= lo) break;
        if (JTextHelper::measureWidth(t.substr(0, mid) + dots) <= width) lo = mid;
        else hi = mid - 1;
    }
    return t.substr(0, lo) + dots;
}

} // namespace

JPTable::JPTable(JSceneGraph& graph) : JControl(graph, "JPTable") {
    const JStyle& st = JStyle::current();
    auto& l = m_graph.getLayout(m_nodeId);
    l.minHeight = st.gridHeaderHeight + 2 * st.gridRowHeight;
    l.minWidth = 4 * st.gridMinColumnWidth;
    setHSizePolicy(JSizePolicyMode::Expanding, 1);
    setVSizePolicy(JSizePolicyMode::Expanding, 1);
    setContextMenu(nullptr);
}

void JPTable::setModel(JPTableModel* model) {
    stopEditing(false);
    m_model = model;
    m_widths.clear();
    m_sortKeys.clear();
    m_selected.clear();
    m_lead = m_anchor = -1;
    refresh();
}

void JPTable::refresh() {
    std::set<std::string> chosen;
    for (const int r : m_selected)
        if (r >= 0 && size_t(r) < m_keys.size()) chosen.insert(m_keys[size_t(r)]);
    const std::string lead = m_lead >= 0 && size_t(m_lead) < m_keys.size() ? m_keys[size_t(m_lead)] : std::string();
    m_keys.clear();
    const int n = m_model ? m_model->rowCount() : 0;
    for (int r = 0; r < n; ++r) m_keys.push_back(m_model->rowKey(r));
    m_selected.clear();
    m_lead = m_anchor = -1;
    for (int r = 0; r < n; ++r) {
        if (chosen.count(m_keys[size_t(r)])) m_selected.insert(r);
        if (!lead.empty() && m_keys[size_t(r)] == lead) m_lead = m_anchor = r;
    }
    if (m_editing && (m_editRow >= n)) stopEditing(false);
    rebuildView();
}

bool JPTable::rowPasses(int row) const {
    if (!m_model->rowShown(row)) return false;
    if (!m_filter) return true;
    for (int c = 0; c < m_model->columnCount(); ++c)
        if (std::regex_search(m_model->text(row, c), *m_filter)) return true;
    return false;
}

int JPTable::compareRows(int a, int b, int c) const {
    if (const std::optional<int> o = m_model->compare(a, b, c)) return *o;
    switch (m_model->column(c).kind) {
        case JPTableModel::Kind::Number: {
            const double x = m_model->number(a, c), y = m_model->number(b, c);
            return x < y ? -1 : x > y ? 1 : 0;
        }
        case JPTableModel::Kind::Boolean: {
            const bool x = m_model->checked(a, c), y = m_model->checked(b, c);
            return x == y ? 0 : (x ? 1 : -1);
        }
        default: return collate(m_model->text(a, c), m_model->text(b, c));
    }
}

void JPTable::rebuildView() {
    m_view.clear();
    if (m_model)
        for (int r = 0; r < m_model->rowCount(); ++r)
            if (rowPasses(r)) m_view.push_back(r);
    if (m_model && !m_sortKeys.empty())
        std::stable_sort(m_view.begin(), m_view.end(), [this](int a, int b) {
            for (const SortKey& k : m_sortKeys) {
                if (k.column >= m_model->columnCount()) continue;
                const int c = compareRows(a, b, k.column);
                if (c != 0) return k.ascending ? c < 0 : c > 0;
            }
            return a < b;
        });
    // Rows the filter hides are no longer chosen, as in Swing.
    std::erase_if(m_selected, [this](int r) { return viewIndexOf(r) < 0; });
    if (m_lead >= 0 && viewIndexOf(m_lead) < 0) m_lead = -1;
    clampScroll();
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

void JPTable::setFilter(const std::string& text) {
    std::string t = text;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    size_t a = 0;
    while (a < t.size() && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
    t = t.substr(a);
    if (t.empty()) {
        m_filter.reset();
    } else {
        try {
            m_filter = std::regex(t, std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error&) {
            return;   // as OpenPnP: an expression not finished yet changes nothing
        }
    }
    m_filterText = text;
    const size_t before = m_selected.size();
    rebuildView();
    if (m_selected.size() != before) selectionChanged();
}

int JPTable::viewIndexOf(int modelRow) const {
    const auto it = std::find(m_view.begin(), m_view.end(), modelRow);
    return it == m_view.end() ? -1 : int(it - m_view.begin());
}

std::vector<int> JPTable::selectedRows() const {
    std::vector<int> out;
    for (const int r : m_view)
        if (m_selected.count(r)) out.push_back(r);
    return out;
}

int JPTable::selectedRow() const {
    return m_selected.size() == 1 ? *m_selected.begin() : -1;
}

void JPTable::selectRow(int modelRow) {
    selectRows(modelRow >= 0 ? std::vector<int> { modelRow } : std::vector<int> {});
}

void JPTable::selectRows(const std::vector<int>& rows) {
    std::set<int> now;
    for (const int r : rows)
        if (viewIndexOf(r) >= 0) now.insert(r);
    const bool changed = now != m_selected;
    m_selected = std::move(now);
    m_lead = m_anchor = m_selected.empty() ? -1 : rows.front();
    if (m_lead >= 0) ensureVisible(viewIndexOf(m_lead));
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    if (changed) onSelectionChanged.emit();
}

void JPTable::clearSelection() {
    selectRows({});
}

void JPTable::selectionChanged() {
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    onSelectionChanged.emit();
}

// ---- geometry ---------------------------------------------------------------

JRect JPTable::bounds() const {
    return m_graph.getLayoutConst(m_nodeId).boundingBox;
}

float JPTable::headerHeight() const { return m_headerShown ? JStyle::current().gridHeaderHeight : 0.f; }

void JPTable::scrollToEnd() {
    m_scrollY = float(m_view.size()) * rowHeight();
    clampScroll();
    invalidate();
}

bool JPTable::atEnd() const {
    const JRect b = bounds();
    const float maxY = std::max(0.f, float(m_view.size()) * rowHeight() - (b.height - headerHeight()));
    return m_scrollY >= maxY - rowHeight() * 0.5f;
}
float JPTable::rowHeight() const { return JStyle::current().gridRowHeight; }

void JPTable::materialiseWidths() const {
    // As a Swing table (AUTO_RESIZE_SUBSEQUENT_COLUMNS): the columns always
    // fill the table's width; a new width shares itself out as they stood.
    if (!m_model) return;
    const int n = m_model->columnCount();
    const JStyle& st = JStyle::current();
    const float room = bounds().width - st.scrollBarWidth;
    if (room <= 0 || n == 0) return;
    if (int(m_widths.size()) != n) {
        m_widths.assign(size_t(n), 0);
        float fixed = 0;
        int shared = 0;
        for (int c = 0; c < n; ++c) {
            const float w = m_model->column(c).width;
            if (w > 0) fixed += w;
            else ++shared;
        }
        for (int c = 0; c < n; ++c) {
            const float w = m_model->column(c).width;
            m_widths[size_t(c)] = w > 0 ? w : (shared ? std::max(0.f, room - fixed) / float(shared) : st.gridDefaultColumnWidth);
        }
    }
    float total = 0;
    for (const float w : m_widths) total += w;
    if (total > 0 && std::fabs(total - room) > 0.5f)
        for (float& w : m_widths) w = std::max(st.gridMinColumnWidth, w * room / total);
}

float JPTable::columnWidth(int c) const {
    materialiseWidths();
    return c >= 0 && size_t(c) < m_widths.size() ? m_widths[size_t(c)] : 0;
}

void JPTable::setColumnWidth(int c, float w) {
    // The columns after it give or take the difference, as in Swing.
    materialiseWidths();
    if (c < 0 || size_t(c) + 1 >= m_widths.size()) return;
    const float least = JStyle::current().gridMinColumnWidth;
    float after = 0;
    for (size_t i = size_t(c) + 1; i < m_widths.size(); ++i) after += m_widths[i];
    const float most = m_widths[size_t(c)] + after - least * float(m_widths.size() - size_t(c) - 1);
    const float now = std::clamp(w, least, std::max(least, most));
    const float give = now - m_widths[size_t(c)];
    m_widths[size_t(c)] = now;
    for (size_t i = size_t(c) + 1; i < m_widths.size(); ++i)
        m_widths[i] = std::max(least, m_widths[i] - give * (after > 0 ? m_widths[i] / after : 0));
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

float JPTable::totalColumnsWidth() const {
    materialiseWidths();
    float w = 0;
    for (const float x : m_widths) w += x;
    return w;
}

float JPTable::columnX(int c) const {
    materialiseWidths();
    float x = 0;
    for (int i = 0; i < c && size_t(i) < m_widths.size(); ++i) x += m_widths[size_t(i)];
    return x;
}

int JPTable::columnAt(float mx) const {
    const JRect b = bounds();
    float x = b.x - m_scrollX;
    for (int c = 0; m_model && c < m_model->columnCount(); ++c) {
        const float w = columnWidth(c);
        if (mx >= x && mx < x + w) return c;
        x += w;
    }
    return -1;
}

int JPTable::dividerAt(float mx, float my) const {
    const JRect b = bounds();
    if (!m_model || my < b.y || my >= b.y + headerHeight()) return -1;
    float x = b.x - m_scrollX;
    for (int c = 0; c < m_model->columnCount(); ++c) {
        x += columnWidth(c);
        if (std::fabs(mx - x) <= JStyle::current().gridResizeGrab) return c;
    }
    return -1;
}

int JPTable::viewRowAt(float my) const {
    const JRect b = bounds();
    const float y = my - b.y - headerHeight() + m_scrollY;
    if (my < b.y + headerHeight() || y < 0) return -1;
    const int v = int(y / rowHeight());
    return v < int(m_view.size()) ? v : -1;
}

int JPTable::rowAt(float my) const {
    const int v = viewRowAt(my);
    return v >= 0 ? m_view[size_t(v)] : -1;
}

JRect JPTable::cellRect(int viewRow, int c) const {
    const JRect b = bounds();
    return { b.x + columnX(c) - m_scrollX, b.y + headerHeight() + float(viewRow) * rowHeight() - m_scrollY, columnWidth(c),
             rowHeight() };
}

void JPTable::clampScroll() {
    const JRect b = bounds();
    const float sb = JStyle::current().scrollBarWidth;
    (void)sb;
    const float maxY = std::max(0.f, float(m_view.size()) * rowHeight() - (b.height - headerHeight()));
    m_scrollY = std::clamp(m_scrollY, 0.f, maxY);
    m_scrollX = 0;   // the columns always fit the width
}

void JPTable::ensureVisible(int v) {
    if (v < 0) return;
    const JRect b = bounds();
    const float visible = b.height - headerHeight();
    // Not laid out yet (its tab behind another): brought into view when it is.
    if (visible < rowHeight()) {
        m_reveal = m_view[size_t(v)];
        return;
    }
    const float top = float(v) * rowHeight();
    if (top < m_scrollY) m_scrollY = top;
    else if (top + rowHeight() > m_scrollY + visible) m_scrollY = top + rowHeight() - visible;
    clampScroll();
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

// ---- drawing ----------------------------------------------------------------

void JPTable::populateRenderPrimitives(JPrimitiveBuffer& buf) {
    const JRect b = bounds();
    const JStyle& st = JStyle::current();
    const bool focused = isFocused();
    buf.pushRectangle(b.x, b.y, b.width, b.height, Colors::Surface1, st.cornerRadius, st.borderWidth,
                      focused ? Colors::Accent : Colors::Border);
    if (!m_model || !JTextHelper::hasAtlas()) return;
    if (m_reveal >= 0) {
        const int r = m_reveal;
        m_reveal = -1;
        ensureVisible(viewIndexOf(r));
    }
    clampScroll();
    const int columns = m_model->columnCount();
    const float hh = headerHeight(), rh = rowHeight(), pad = st.gridCellPadding;
    const float sb = st.scrollBarWidth;
    const float innerW = b.width - sb, innerH = b.height;
    const float lh = JTextHelper::lineHeight();

    // The headings, each with its sort marks.
    buf.pushClip(b.x, b.y, innerW, innerH);
    buf.pushRectangle(b.x, b.y, innerW, hh, Colors::Surface3);
    for (int c = 0; c < (m_headerShown ? columns : 0); ++c) {
        const float x = b.x + columnX(c) - m_scrollX, w = columnWidth(c);
        float fade = 1.f;
        int keyIndex = -1;
        for (size_t k = 0; k < m_sortKeys.size(); ++k) {
            if (m_sortKeys[k].column == c) {
                keyIndex = int(k);
                break;
            }
            fade *= kSortKeyFade;
        }
        const float reserve = keyIndex >= 0 ? st.gridSortGlyphWidth : 0.f;
        // Centred, as a Swing table's headings are.
        const float room = std::max(1.f, w - 2 * pad - reserve);
        const std::string name = elided(m_model->column(c).name, room);
        const float tx = x + pad + std::max(0.f, (room - JTextHelper::measureWidth(name)) * 0.5f);
        JTextHelper::pushText(buf, tx, b.y + (hh - lh) * 0.5f, name, Colors::GridHeaderText, room);
        if (keyIndex >= 0) {
            uint8_t ink[4] = { Colors::GridHeaderText[0], Colors::GridHeaderText[1], Colors::GridHeaderText[2],
                               uint8_t(float(Colors::GridHeaderText[3]) * fade) };
            JArrow::draw(buf, x + w - pad - st.gridSortGlyphWidth * 0.5f, b.y + hh * 0.5f,
                         m_sortKeys[size_t(keyIndex)].ascending ? JArrow::Direction::Up : JArrow::Direction::Down, ink);
        }
        if (c > 0) buf.pushRectangle(x, b.y, st.borderWidth, hh, Colors::Border);
    }
    buf.popClip();

    // The rows in view.
    buf.pushClip(b.x, b.y + hh, innerW, innerH - hh);
    JVectorCanvas vg;
    const int first = std::max(0, int(m_scrollY / rh));
    const int last = std::min(int(m_view.size()) - 1, int((m_scrollY + innerH - hh) / rh) + 1);
    for (int v = first; v <= last; ++v) {
        const int r = m_view[size_t(v)];
        const float y = b.y + hh + float(v) * rh - m_scrollY;
        const bool chosen = m_selected.count(r) != 0;
        if (s_alternateRows && v % 2 == 1) buf.pushRectangle(b.x, y, innerW, rh, Colors::RowAltBg);
        if (chosen) {
            uint8_t sel[4] = { Colors::Accent[0], Colors::Accent[1], Colors::Accent[2], 90 };
            buf.pushRectangle(b.x, y, innerW, rh, sel);
        }
        for (int c = 0; c < columns; ++c) {
            const JRect cell = cellRect(v, c);
            if (c > 0) buf.pushRectangle(cell.x, y, st.borderWidth, rh, Colors::GridLine);
            if (m_editing && r == m_editRow && c == m_editColumn) continue;   // drawn below
            const JPTableModel::Kind kind = m_model->column(c).kind;
            if (kind == JPTableModel::Kind::Boolean) {
                const float s = rh * kTickShare;
                const float bx = cell.x + (cell.width - s) * 0.5f, by = y + (rh - s) * 0.5f;
                const bool dimmed = !chosen && m_model->cellDimmed(r, c);
                vg.strokeRoundedRect(bx, by, s, s, st.borderWidth * 2, st.borderWidth,
                                     JPaint::solid(colour(dimmed ? Colors::MutedText : Colors::Border)));
                if (m_model->checked(r, c)) {
                    vg.fillRoundedRect(bx, by, s, s, st.borderWidth * 2, JPaint::solid(colour(dimmed ? Colors::MutedText : Colors::Accent)));
                    std::vector<JVectorCanvas::JVec2> tick { { bx + s * 0.22f, by + s * 0.52f }, { bx + s * 0.42f, by + s * 0.72f },
                                                             { bx + s * 0.78f, by + s * 0.3f } };
                    vg.strokePolyline(tick, st.borderWidth * 2, JPaint::solid(colour(Colors::HighlightedText)));
                }
                continue;
            }
            const JPTableModel::Column col = m_model->column(c);
            const uint8_t* ink = Colors::LabelText;
            if (const uint8_t* fill = m_model->cellTint(r, c)) {
                const JColor tint = rgba(fill[0], fill[1], fill[2], uint8_t(float(fill[3]) * kHighlightAlpha));
                buf.pushRectangle(cell.x + st.borderWidth, y, cell.width - st.borderWidth, rh - st.borderWidth, tint.data());
                ink = Colors::TextPrimary;
            }
            if (const uint8_t* own = m_model->cellInk(r, c)) ink = own;
            float tx = cell.x + pad;
            // An icon before the text, as tall as a line.
            if (const std::string icon = m_model->cellIcon(r, c); !icon.empty())
                if (JPOpenPnpIcons* icons = JPOpenPnpIcons::instance()) {
                    const std::string shown = m_model->displayText(r, c);
                    // Its leading spaces (an indent) before the icon.
                    const size_t lead = shown.find_first_not_of(' ');
                    tx += JTextHelper::measureWidth(shown.substr(0, lead == std::string::npos ? shown.size() : lead));
                    const TextureHandle tex = icons->texture(icon, int(lh * kIconOversample), false);
                    if (tex != kNullTexture) buf.pushImage(tx, y + (rh - lh) * 0.5f, lh, lh, tex);
                    tx += lh + st.spacing * 0.5f;
                }
            const float room = std::max(1.f, cell.x + cell.width - pad - tx);
            std::string full = m_model->displayText(r, c);
            if (!m_model->cellIcon(r, c).empty()) {
                const size_t lead = full.find_first_not_of(' ');
                full = lead == std::string::npos ? std::string() : full.substr(lead);
            }
            if (col.decimalAligned) {
                // The point at the same place in every row, the widest number centred.
                const size_t dot = full.find('.');
                const std::string whole = full.substr(0, dot);
                const float wholeW = JTextHelper::measureWidth(kAlignedWhole);
                const float boxW = wholeW + JTextHelper::measureWidth(kAlignedRest);
                if (boxW <= cell.width - 2 * pad) {
                    const float dotX = cell.x + (cell.width - boxW) * 0.5f + wholeW;
                    tx = std::max(tx, dotX - JTextHelper::measureWidth(whole));
                } else {
                    // Too narrow to line up: against the right, as a number.
                    tx = std::max(tx, cell.x + cell.width - pad - JTextHelper::measureWidth(full));
                }
                // A hair of slack: the room is worked out from the same text's width.
                const float fit = cell.x + cell.width - pad - tx + 1.f;
                JTextHelper::pushText(buf, tx, y + (rh - lh) * 0.5f, elided(full, fit), ink, std::max(1.f, fit));
                continue;
            }
            const std::string t = elided(full, room);
            const float tw = JTextHelper::measureWidth(t);
            JPTableModel::Align align = col.align;
            if (align == JPTableModel::Align::Auto)
                align = kind == JPTableModel::Kind::Number ? JPTableModel::Align::Right : JPTableModel::Align::Left;
            if (align == JPTableModel::Align::Right) tx = std::max(tx, cell.x + cell.width - pad - tw);
            else if (align == JPTableModel::Align::Center) tx = std::max(tx, cell.x + (cell.width - tw) * 0.5f);
            JTextHelper::pushText(buf, tx, y + (rh - lh) * 0.5f, t, ink, room);
        }
        buf.pushRectangle(b.x, y + rh - st.borderWidth, innerW, st.borderWidth, Colors::GridLine);
    }
    vg.flush(buf);

    // The cell being edited: its text, the chosen part and the caret.
    if (m_editing) {
        const int v = viewIndexOf(m_editRow);
        if (v >= 0) {
            const JRect cell = cellRect(v, m_editColumn);
            buf.pushRectangle(cell.x, cell.y, cell.width, cell.height, Colors::Surface1, 0.f, st.borderWidth * 2, Colors::Accent);
            const std::string& t = m_edit.text();
            // Text wider than the cell scrolls along it, the caret always inside (as a text field does).
            const float room = std::max(1.f, cell.width - 2 * pad);
            const float caretX = JTextHelper::measureWidth(t.substr(0, m_edit.caret()));
            const float fullW = JTextHelper::measureWidth(t);
            if (caretX - m_editScroll > room) m_editScroll = caretX - room;
            if (caretX < m_editScroll) m_editScroll = caretX;
            m_editScroll = std::max(0.f, std::min(m_editScroll, std::max(0.f, fullW - room)));
            const float tx = cell.x + pad - m_editScroll, ty = cell.y + (rh - lh) * 0.5f;
            buf.pushClip(cell.x + st.borderWidth, cell.y, std::max(0.f, cell.width - 2 * st.borderWidth), cell.height);
            if (m_edit.hasSelection()) {
                const float x0 = tx + JTextHelper::measureWidth(t.substr(0, m_edit.selectionStart()));
                const float x1 = tx + JTextHelper::measureWidth(t.substr(0, m_edit.selectionEnd()));
                uint8_t sel[4] = { Colors::Accent[0], Colors::Accent[1], Colors::Accent[2], 120 };
                buf.pushRectangle(x0, ty, std::max(1.f, x1 - x0), lh, sel);
            }
            JTextHelper::pushText(buf, tx, ty, t, Colors::ControlText, fullW + 1);
            buf.pushRectangle(tx + caretX, ty, st.borderWidth, lh, Colors::ControlText);
            buf.popClip();
        }
    }
    // Where a dragged row goes.
    if (m_dropBefore >= 0) {
        const float y = b.y + hh + float(m_dropBefore) * rh - m_scrollY;
        buf.pushRectangle(b.x, y - st.borderWidth, innerW, st.borderWidth * 2, Colors::Accent);
    }
    buf.popClip();

    // Scroll bars.
    const float contentH = float(m_view.size()) * rh, viewH = innerH - hh;
    if (contentH > viewH) {
        const float track = viewH, thumb = std::max(sb * 2, track * viewH / contentH);
        const float y = b.y + hh + (track - thumb) * (m_scrollY / std::max(1.f, contentH - viewH));
        buf.pushRectangle(b.x + innerW, b.y + hh, sb, track, Colors::Surface2);
        buf.pushRectangle(b.x + innerW, y, sb, thumb, Colors::Surface3, sb * 0.5f);
    }
}

// ---- mouse ------------------------------------------------------------------

void JPTable::clickHeader(int c) {
    // As Swing's DefaultRowSorter.toggleSortOrder.
    auto it = std::find_if(m_sortKeys.begin(), m_sortKeys.end(), [c](const SortKey& k) { return k.column == c; });
    if (it == m_sortKeys.end()) {
        m_sortKeys.insert(m_sortKeys.begin(), { c, true });
    } else if (it == m_sortKeys.begin()) {
        it->ascending = !it->ascending;
    } else {
        m_sortKeys.erase(it);
        m_sortKeys.insert(m_sortKeys.begin(), { c, true });
    }
    if (m_sortKeys.size() > kMostSortKeys) m_sortKeys.resize(kMostSortKeys);
    rebuildView();
}

void JPTable::selectView(int v, bool shift, bool ctrl) {
    if (v < 0 || size_t(v) >= m_view.size()) return;
    const int r = m_view[size_t(v)];
    if (shift && m_anchor >= 0 && viewIndexOf(m_anchor) >= 0) {
        const int a = viewIndexOf(m_anchor);
        if (!ctrl) m_selected.clear();
        for (int i = std::min(a, v); i <= std::max(a, v); ++i) m_selected.insert(m_view[size_t(i)]);
    } else if (ctrl) {
        if (!m_selected.erase(r)) m_selected.insert(r);
        m_anchor = r;
    } else {
        m_selected = { r };
        m_anchor = r;
    }
    m_lead = r;
    ensureVisible(v);
    selectionChanged();
}

void JPTable::handleMousePress(float mx, float my) {
    if (!m_model || !isPointInside(mx, my)) return;
    onClicked.emit();
    const JRect b = bounds();
    const float sb = JStyle::current().scrollBarWidth;
    if (mx >= b.x + b.width - sb && my >= b.y + headerHeight()) {
        m_draggingV = true;
        m_dragFromY = my;
        m_dragFromScroll = m_scrollY;
        return;
    }
    if (const int d = dividerAt(mx, my); d >= 0) {
        stopEditing(true);
        m_resizing = d;
        m_resizeFromX = mx;
        m_resizeFromW = columnWidth(d);
        return;
    }
    if (my < b.y + headerHeight()) {
        stopEditing(true);
        if (const int c = columnAt(mx); c >= 0) clickHeader(c);
        return;
    }
    const int v = viewRowAt(my);
    const int c = columnAt(mx);
    if (v < 0) {
        stopEditing(true);
        return;
    }
    const int r = m_view[size_t(v)];
    if (m_editing) {
        if (r == m_editRow && c == m_editColumn) {
            // A click in the cell being edited places the caret.
            const JRect cell = cellRect(v, c);
            const std::string& t = m_edit.text();
            size_t best = 0;
            const float left = cell.x + JStyle::current().gridCellPadding - m_editScroll;
            float bestD = std::fabs(mx - left);
            for (size_t i = 0; i < t.size();) {
                i = m_edit.nextCharStart(i);
                const float d = std::fabs(mx - (left + JTextHelper::measureWidth(t.substr(0, i))));
                if (d < bestD) {
                    bestD = d;
                    best = i;
                }
            }
            m_edit.setCaret(best, JWidget::s_shiftDown);
            m_graph.invalidateNode(m_nodeId, DirtySelf);
            return;
        }
        stopEditing(true);
    }
    if (c >= 0) m_leadColumn = c;
    selectView(v, JWidget::s_shiftDown, JWidget::s_ctrlDown);
    if (canDragRows() && !JWidget::s_shiftDown && !JWidget::s_ctrlDown) {
        m_rowDrag = r;
        m_rowDragFromY = my;
    }
    if (c < 0 || JWidget::s_shiftDown || JWidget::s_ctrlDown) return;
    const JPTableModel::Kind kind = m_model->column(c).kind;
    const bool editable = m_model->editable(r, c);
    if (kind == JPTableModel::Kind::Boolean && editable) {
        m_model->setChecked(r, c, !m_model->checked(r, c));
        refresh();
    } else if (kind == JPTableModel::Kind::Choice && editable) {
        openChoices(r, c);
    } else if (kind == JPTableModel::Kind::Picker && editable) {
        m_model->pick(r, c);
    } else if (JWidget::s_doubleClick) {
        if (editable && kind != JPTableModel::Kind::Boolean) startEditing(r, c, nullptr);
        else onRowActivated.emit(r);
    }
}

bool JPTable::canDragRows() const {
    return m_model && m_model->reorderable() && m_sortKeys.empty() && !m_filter && int(m_view.size()) == m_model->rowCount();
}

void JPTable::handleMouseRelease(float mx, float my) {
    m_resizing = -1;
    m_draggingV = false;
    const int from = m_rowDrag, before = m_dropBefore;
    m_rowDrag = m_dropBefore = -1;
    if (from >= 0 && before >= 0) {
        m_graph.invalidateNode(m_nodeId, DirtySelf);
        if (before != from && before != from + 1) {
            m_model->reorder(from, before);
            refresh();
            selectRow(before > from ? before - 1 : before);
        }
    }
    JControl::handleMouseRelease(mx, my);
}

void JPTable::handleMouseMove(float mx, float my) {
    if (m_resizing >= 0) {
        JWidget::s_hoverCursor = JPlatformCursor::ResizeLeftRight;
        setColumnWidth(m_resizing, m_resizeFromW + (mx - m_resizeFromX));
        return;
    }
    if (m_rowDrag >= 0) {
        // Moved half a row: the drag is on, the line where it goes shown.
        if (m_dropBefore >= 0 || std::fabs(my - m_rowDragFromY) > rowHeight() * 0.5f) {
            const JRect b = bounds();
            const float at = (my - (b.y + headerHeight()) + m_scrollY) / rowHeight();
            m_dropBefore = std::clamp(int(std::floor(at + 0.5f)), 0, int(m_view.size()));
            m_graph.invalidateNode(m_nodeId, DirtySelf);
        }
        return;
    }
    if (m_draggingV) {
        const JRect b = bounds();
        const float viewH = b.height - headerHeight();
        const float contentH = float(m_view.size()) * rowHeight();
        if (contentH > viewH) {
            m_scrollY = m_dragFromScroll + (my - m_dragFromY) * contentH / viewH;
            clampScroll();
            m_graph.invalidateNode(m_nodeId, DirtySelf);
        }
        return;
    }
    if (dividerAt(mx, my) >= 0) JWidget::s_hoverCursor = JPlatformCursor::ResizeLeftRight;
    // A heading's tooltip over its heading; a cell's (if it has one) over the rows.
    const JRect b = bounds();
    std::string tip;
    if (m_model && my >= b.y && my < b.y + headerHeight()) {
        if (const int c = columnAt(mx); c >= 0) tip = m_model->column(c).tooltip;
    } else if (m_model) {
        const int r = rowAt(my), c = columnAt(mx);
        if (r >= 0 && c >= 0) tip = m_model->cellTooltip(r, c);
    }
    setTooltip(tip);
    JControl::handleMouseMove(mx, my);
}

bool JPTable::handleScroll(float mx, float my, float wheel) {
    if (!isPointInside(mx, my)) return false;
    m_scrollY -= wheel * rowHeight() * 3;
    clampScroll();
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    return true;
}

void JPTable::prepareContextMenu(float mx, float my) {
    if (onContextMenu) onContextMenu(rowAt(my));
    (void)mx;
}

void JPTable::onFocusEvent(bool focused) {
    if (!focused) stopEditing(true);
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

// ---- keys -------------------------------------------------------------------

void JPTable::moveLead(int v, bool extend) {
    if (m_view.empty()) return;
    v = std::clamp(v, 0, int(m_view.size()) - 1);
    selectView(v, extend, false);
}

bool JPTable::handleKeyEvent(const JKeyEvent& ke) {
    if (!ke.pressed || !m_model) return false;
    using K = JKeyEvent::JKey;
    if (m_editing) {
        if (ke.key == K::Escape) {
            stopEditing(false);
            return true;
        }
        if (ke.key == K::Tab || ke.key == K::BackTab) {
            const bool back = ke.key == K::BackTab || ke.shift;
            const int row = m_editRow, column = m_editColumn;
            if (!stopEditing(true, true)) return true;
            // On to the next cell that can be edited (Shift: the one before), along the row and on to the next
            // row's, as a spreadsheet goes: a text or number cell opened with its contents chosen, ready to type
            // over; another kind (a choice, a tick box) chosen, for F2 or Space.
            const int columns = m_model->columnCount(), rows = int(m_view.size());
            int v = viewIndexOf(row), c = column;
            for (int steps = 0; steps < columns * std::max(1, rows) && v >= 0; ++steps) {
                c += back ? -1 : 1;
                if (c >= columns) { c = 0; ++v; }
                if (c < 0) { c = columns - 1; --v; }
                if (v < 0 || v >= rows) break;
                const int r = m_view[size_t(v)];
                if (!m_model->editable(r, c)) continue;
                m_lead = r;
                m_leadColumn = c;
                m_selected = { r };
                selectionChanged();
                const JPTableModel::Kind kind = m_model->column(c).kind;
                if (kind == JPTableModel::Kind::Text || kind == JPTableModel::Kind::Number) startEditing(r, c, nullptr);
                else ensureVisible(v);
                break;
            }
            m_graph.invalidateNode(m_nodeId, DirtySelf);
            return true;
        }
        const JTextEditCore::KeyResult res = m_edit.handleKey(ke);
        if (res.returnPressed) {
            stopEditing(true, true);
            return true;
        }
        if (res.consumed) m_graph.invalidateNode(m_nodeId, DirtySelf);
        return res.consumed;
    }
    if (onKey && onKey(ke)) return true;
    const int lead = viewIndexOf(m_lead);
    const int page = std::max(1, int((bounds().height - headerHeight()) / rowHeight()) - 1);
    switch (ke.key) {
        case K::Up:       moveLead(lead < 0 ? 0 : lead - 1, ke.shift); return true;
        case K::Down:     moveLead(lead < 0 ? 0 : lead + 1, ke.shift); return true;
        case K::PageUp:   moveLead(lead - page, ke.shift); return true;
        case K::PageDown: moveLead(lead + page, ke.shift); return true;
        case K::Home:     if (ke.ctrl) { moveLead(0, ke.shift); return true; } break;
        case K::End:      if (ke.ctrl) { moveLead(int(m_view.size()) - 1, ke.shift); return true; } break;
        case K::Left:     m_leadColumn = std::max(0, m_leadColumn - 1); m_graph.invalidateNode(m_nodeId, DirtySelf); return true;
        case K::Right:    m_leadColumn = std::min(m_model->columnCount() - 1, m_leadColumn + 1); m_graph.invalidateNode(m_nodeId, DirtySelf); return true;
        default: break;
    }
    if (ke.ctrl && ke.key == K::A) {
        m_selected.clear();
        for (const int r : m_view) m_selected.insert(r);
        selectionChanged();
        return true;
    }
    if (ke.ctrl && ke.key == K::C) {
        copySelection();
        return true;
    }
    if (m_lead < 0) return false;
    const JPTableModel::Kind kind = m_model->column(m_leadColumn).kind;
    const bool editable = m_model->editable(m_lead, m_leadColumn);
    if (ke.key == K::F2 && editable) {
        if (kind == JPTableModel::Kind::Choice) openChoices(m_lead, m_leadColumn);
        else if (kind == JPTableModel::Kind::Picker) m_model->pick(m_lead, m_leadColumn);
        else if (kind == JPTableModel::Kind::Boolean) {
            m_model->setChecked(m_lead, m_leadColumn, !m_model->checked(m_lead, m_leadColumn));
            refresh();
        } else startEditing(m_lead, m_leadColumn, nullptr);
        return true;
    }
    if (ke.key == K::Space && kind == JPTableModel::Kind::Boolean && editable) {
        m_model->setChecked(m_lead, m_leadColumn, !m_model->checked(m_lead, m_leadColumn));
        refresh();
        return true;
    }
    // Typing on a text cell starts editing it with what was typed.
    if (!ke.ctrl && !ke.alt && ke.utf8[0] && static_cast<unsigned char>(ke.utf8[0]) >= 0x20 && editable
        && (kind == JPTableModel::Kind::Text || kind == JPTableModel::Kind::Number)) {
        const std::string typed = ke.utf8;
        startEditing(m_lead, m_leadColumn, &typed);
        return true;
    }
    return false;
}

void JPTable::copySelection() const {
    std::string out;
    for (const int r : selectedRows()) {
        for (int c = 0; c < m_model->columnCount(); ++c) {
            if (c) out += '\t';
            out += m_model->column(c).kind == JPTableModel::Kind::Boolean ? (m_model->checked(r, c) ? "true" : "false")
                                                                          : m_model->text(r, c);
        }
        out += '\n';
    }
    JWidget::clipboardSet(out);
}

// ---- editing ----------------------------------------------------------------

void JPTable::startEditing(int r, int c, const std::string* typed) {
    stopEditing(true);
    m_editing = true;
    m_editRow = r;
    m_editColumn = c;
    m_leadColumn = c;
    m_editScroll = 0;
    if (typed) {
        m_edit.setText(*typed);
    } else {
        m_edit.setText(m_model->text(r, c));
        m_edit.selectAll();   // as OpenPnP's AutoSelectTextTable
    }
    ensureVisible(viewIndexOf(r));
    m_graph.invalidateNode(m_nodeId, DirtySelf);
}

bool JPTable::stopEditing(bool keep, bool stayIfRefused) {
    if (!m_editing) return true;
    const int r = m_editRow, c = m_editColumn;
    if (keep && m_model && r < m_model->rowCount() && m_edit.text() != m_model->text(r, c)) {
        std::string error;
        if (!m_model->setText(r, c, m_edit.text(), error)) {
            if (onEditRefused && !error.empty()) onEditRefused(error);
            // As a Swing table: a value it cannot take keeps the cell open.
            if (stayIfRefused) {
                m_graph.invalidateNode(m_nodeId, DirtySelf);
                return false;
            }
        }
        m_editing = false;
        m_editRow = m_editColumn = -1;
        refresh();
        return true;
    }
    m_editing = false;
    m_editRow = m_editColumn = -1;
    m_graph.invalidateNode(m_nodeId, DirtySelf);
    return true;
}

void JPTable::openChoices(int r, int c) {
    if (!JComboBox::onOpenPopupHook) return;
    const std::vector<std::string> choices = m_model->choices(r, c);
    if (choices.empty()) return;
    const std::string now = m_model->text(r, c);
    // Its list opens under the cell, as wide as its longest choice (never narrower than the cell, nor wider
    // than the table).
    JRect cell = cellRect(viewIndexOf(r), c);
    const JStyle& st = JStyle::current();
    float widest = 0;
    for (const std::string& choice : choices) widest = std::max(widest, JTextHelper::measureWidth(choice));
    const JRect b = bounds();
    cell.width = std::min(std::max(cell.width, widest + 2 * st.spacing + st.scrollBarWidth), b.width);
    cell.x = std::min(cell.x, b.x + b.width - cell.width);
    m_choiceCombo = std::make_unique<JComboBox>(m_graph, choices, cell.width, cell.height);
    m_choiceCombo->setBounds(cell);
    for (size_t i = 0; i < choices.size(); ++i)
        if (choices[i] == now) m_choiceCombo->setCurrentIndex(int(i));
    m_choiceCombo->onIndexChanged.connect([this, r, c](int index) {
        if (!m_model || r >= m_model->rowCount() || index < 0) return;
        m_model->setChoice(r, c, index);
        refresh();
    });
    JComboBox::onOpenPopupHook(m_choiceCombo.get());
}

} // inline namespace jf
