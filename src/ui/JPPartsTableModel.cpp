// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartsTableModel.h"

#include "JPSourceColumn.h"

#include "JPChooserTable.h"
#include "JPLengthCell.h"

#include "setup/JPLibrarySearch.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

enum Col { kSource, kId, kName, kDescription, kValue, kMpn, kHeight, kDepth, kPackage, kSpeed, kBottomVision, kFiducialVision, kPlacements,
           kFeeders, kPackagings, kSuppliers, kMakers, kStock, kDatasheet, kColumns };
// Each column's field (JPFieldSet::Of::Parts), in their order.
constexpr const char* kKeys[kColumns] = { "source", "id", "name", "description", "value", "mpn", "height", "depth", "package",
                                          "speed", "bottomVision", "fiducialVision", "placements", "feeders", "packaging",
                                          "suppliers", "manufacturer", "stock", "datasheet" };

// OpenPnP's PercentConverter.
std::string percent(double d) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f%%", d * 100);
    return buf;
}

} // namespace

JPPartsTableModel::JPPartsTableModel(JPConfiguration& config) : m_config(config) { reload(); }

void JPPartsTableModel::reload() {
    m_usedBy.clear();
    for (const JPCatalog::Part& e : JPCatalog::parts(m_config)) m_usedBy.push_back(e.usedBy);
    // Each part's open lots, read once here (a cell is drawn many times).
    m_stock.clear();
    for (const auto& p : m_config.parts())
        if (std::vector<JPStockLot> lots = m_config.stock().lots(p->id, false); !lots.empty())
            m_stock.emplace(p->id, std::move(lots));
}

const std::vector<std::shared_ptr<JPBoard>>& JPPartsTableModel::usedBy(int row) const {
    static const std::vector<std::shared_ptr<JPBoard>> none;
    return row >= 0 && size_t(row) < m_usedBy.size() ? m_usedBy[size_t(row)] : none;
}

bool JPPartsTableModel::rowShown(int row) const {
    if (m_show == kShowAll) return true;
    if (m_show == kShowUsed) return !usedBy(row).empty();
    return JPCatalog::uses(usedBy(row), m_show);
}

std::vector<std::string> JPPartsTableModel::showChoices() const {
    std::vector<std::string> out { kShowAll, kShowUsed };
    for (const auto& b : m_config.boards()) out.push_back(b->scopeName());
    return out;
}

std::string JPPartsTableModel::cellIcon(int row, int c) const {
    if (c != kSource) return {};
    return usedBy(row).empty() ? "library" : "board-source";
}

std::string JPPartsTableModel::cellTooltip(int row, int c) const {
    if (c >= kPackagings) {
        // Every value in full, a line each.
        std::string all;
        for (const auto& [shortForm, full] : values(row, c)) all += (all.empty() ? "" : "\n") + full;
        return all;
    }
    if (c != kSource) return {};
    const std::string boards = JPCatalog::boardNames(usedBy(row));
    return boards.empty() ? "In the library; no open board uses it" : "In the library; used by the open board(s) " + boards;
}

std::string JPPartsTableModel::displayText(int row, int c) const {
    if (c >= kPackagings) {
        // The first, after how many there are when there are more (in front, so a narrow column still says
        // so; all of them when pointed at).
        const auto v = values(row, c);
        if (v.empty()) return {};
        return v.size() == 1 ? v.front().first : "(" + std::to_string(v.size()) + ") " + v.front().first;
    }
    return c == kSource ? std::string() : text(row, c);   // its icon alone (its text sorts and searches)
}

int JPPartsTableModel::columnCount() const { return kColumns; }

JPTableModel::Column JPPartsTableModel::column(int c) const {
    Column col = heading(c);
    if (c >= 0 && c < kColumns) col.key = kKeys[c];
    return col;
}

JPTableModel::Column JPPartsTableModel::heading(int c) const {
    switch (c) {
        case kSource:         return JPSourceColumn::column();
        case kId:             return { "ID", "What placements, feeders and jobs know it by (a new part's a GUID)", Kind::Text };
        case kName:
            return { "Name", "What it is called where it is shown, unique in the library; a new part's from where it came "
                             "(a board's name for it, or the name it was given), and yours to change", Kind::Text };
        case kDescription:    return { "Description", "", Kind::Text };
        case kValue:          return { "Value", "Its electrical value as written (100n, 4k7); the Library page has the rest", Kind::Text };
        case kMpn:
            return { "MPN", "Its manufacturer's part number, the first of its identifiers (the Library page has them all)",
                     Kind::Text };
        case kHeight:         return { "Height", "Part height; the distance between board surface and pick position.", Kind::Text };
        case kDepth:
            return { "Through-Board Depth",
                     "The length of any through-board features on the part. That is, the size any feature which is "
                     "below the board surface when placed. This is added to the Part Height to determine how high the "
                     "nozzle needs to be lifted when processing Dynamic Safe Z.\nThis should be zero for purely "
                     "surface-mount parts.",
                     Kind::Text };
        case kPackage:        return { "Package", "", Kind::Choice };
        case kSpeed:          return { "Speed %", "", Kind::Text };
        case kBottomVision:   return { "BottomVision", "", Kind::Choice };
        case kFiducialVision: return { "FiducialVision", "", Kind::Choice };
        case kPlacements:     return { "Placements", "", Kind::Number };
        case kFeeders:        return { "Feeders", "", Kind::Number };
        // More about each part, hidden until chosen (Columns…): how many and the first of each, all of them
        // when pointed at.
        case kPackagings:
            return more("Packaging", "How it comes: each of its Packagings (the Library tab), kind, tape, pitch, rotation");
        case kSuppliers:      return more("Suppliers", "Where it is bought: each of its Offers (the Library tab), supplier and SKU");
        case kMakers:
            return more("Manufacturer", "Who makes it: each of its manufacturer's part numbers (the Library tab's Identifiers)");
        case kStock:          return more("Stock", "Its stock lots: how many, how many on hand, and where each is (a feeder or the shelf)");
        case kDatasheet:      return more("Datasheet", "Its datasheet: a link or a file (the Library tab)");
    }
    return {};
}

JPPartsTableModel::Column JPPartsTableModel::more(const char* name, const char* tooltip) {
    Column c { name, tooltip, Kind::Text };
    c.hiddenAtFirst = true;
    return c;
}

std::vector<std::pair<std::string, std::string>> JPPartsTableModel::values(int row, int c) const {
    // Each value, short (as the cell shows it) and in full (as its tooltip says it).
    std::vector<std::pair<std::string, std::string>> out;
    const JPPart* p = part(row);
    if (!p) return out;
    auto n = [](double v) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", v);
        return std::string(buf);
    };
    auto tape = [](const std::string& kind) { return kind == "Cut tape" || kind == "Reel"; };
    switch (c) {
        case kPackagings:
            for (const JPPart::Packaging& k : p->packagings) {
                std::string shortForm = k.kind, full = k.kind;
                if (tape(k.kind)) {
                    shortForm += " " + n(k.tapeWidthMm) + "/" + n(k.pitchMm);
                    full += ", " + n(k.tapeWidthMm) + " mm " + k.tapeType + " tape, " + n(k.pitchMm) + " mm pitch";
                }
                full += ", " + n(k.rotationDeg) + "°";
                if (k.quantity > 0) full += ", " + std::to_string(k.quantity) + " a " + (k.kind == "Cut tape" ? "strip" : k.kind == "Reel" ? "reel" : "pack");
                if (!k.note.empty()) full += " (" + k.note + ")";
                out.push_back({ shortForm, full });
            }
            break;
        case kSuppliers:
            for (const JPPart::Offer& o : p->offers) {
                const std::string shortForm = o.supplier + (o.sku.empty() ? "" : " " + o.sku);
                std::string full = shortForm;
                if (!o.packaging.empty()) full += ", " + o.packaging;
                if (o.moq > 0) full += ", MOQ " + std::to_string(o.moq);
                if (!o.priceBreaks.empty()) full += ", " + o.priceBreaks;
                if (!o.lastPrice.empty()) full += ", last " + o.lastPrice + (o.lastWhen.empty() ? "" : " (" + o.lastWhen + ")");
                if (!o.link.empty()) full += ", " + o.link;
                out.push_back({ shortForm, full });
            }
            break;
        case kMakers:
            for (const JPPart::Identifier& i : p->identifiers)
                if (i.kind == "mpn") {
                    const std::string s = i.org.empty() ? i.code : i.org + " " + i.code;
                    out.push_back({ s, s });
                }
            break;
        case kStock: {
            const auto at = m_stock.find(p->id);
            if (at == m_stock.end()) break;
            long long total = 0;
            int loaded = 0;
            for (const JPStockLot& l : at->second) {
                total += l.onHand;
                if (!l.feederId.empty()) ++loaded;
            }
            // One summary, then each lot in the tooltip.
            std::string summary = std::to_string(at->second.size()) + (at->second.size() == 1 ? " lot, " : " lots, ")
                                  + std::to_string(total) + " on hand";
            if (loaded) summary += ", " + std::to_string(loaded) + " on a feeder";
            std::string full;
            for (const JPStockLot& l : at->second) {
                const JPFeeder* f = l.feederId.empty() ? nullptr : m_config.feeder(l.feederId);
                std::string s = (l.label.empty() ? std::string("a lot") : l.label) + ": " + std::to_string(l.onHand) + " on hand";
                if (!l.supplier.empty()) s += ", " + l.supplier + (l.sku.empty() ? "" : " " + l.sku);
                s += f ? ", on " + f->name() : l.location.empty() ? std::string(", on the shelf") : ", kept at " + l.location;
                full += (full.empty() ? "" : "\n") + s;
            }
            out.push_back({ summary, full });
            break;
        }
        case kDatasheet:
            if (!p->datasheet.empty()) out.push_back({ p->datasheet, p->datasheet });
            break;
        default: break;
    }
    return out;
}

int JPPartsTableModel::rowCount() const { return int(m_config.parts().size()); }

JPPart* JPPartsTableModel::part(int row) const {
    return row >= 0 && size_t(row) < m_config.parts().size() ? m_config.parts()[size_t(row)].get() : nullptr;
}

int JPPartsTableModel::rowOf(const JPPart* p) const {
    for (size_t i = 0; i < m_config.parts().size(); ++i)
        if (m_config.parts()[i].get() == p) return int(i);
    return -1;
}

std::string JPPartsTableModel::rowKey(int row) const {
    const JPPart* p = part(row);
    return p ? p->id : std::string();
}

std::string JPPartsTableModel::text(int row, int c) const {
    const JPPart* p = part(row);
    if (!p) return {};
    switch (c) {
        case kSource: {
            const std::string boards = JPCatalog::boardNames(usedBy(row));
            return boards.empty() ? std::string("Library") : boards;
        }
        case kId:          return p->id;
        case kName:        return p->shownName();
        case kDescription: return p->name.value_or("");
        case kValue:       return p->value;
        case kMpn:
            for (const auto& i : p->identifiers)
                if (i.kind == "mpn") return i.code;
            return "";
        case kHeight:      return JPLengthCell::text(p->height, true);
        case kDepth:       return JPLengthCell::text(p->throughBoardDepth, true);
        case kPackage:     return m_config.package(p->packageId) ? m_config.package(p->packageId)->id : std::string();
        case kSpeed:       return percent(p->speed);
        case kBottomVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->bottomVisionId);
            return v ? v->name : std::string();
        }
        case kFiducialVision: {
            const JPVisionSettings* v = m_config.visionSettings(p->fiducialVisionId);
            return v ? v->name : std::string();
        }
        case kPlacements: return std::to_string(m_config.placementCount(p->id));
        case kFeeders:    return std::to_string(m_config.feederCount(p->id));
        case kPackagings: case kSuppliers: case kMakers: case kStock: case kDatasheet: {
            // Every value, for searching and sorting.
            std::string all;
            for (const auto& [shortForm, full] : values(row, c)) all += (all.empty() ? "" : " · ") + shortForm;
            return all;
        }
    }
    return {};
}

double JPPartsTableModel::number(int row, int c) const {
    if (c == kPlacements) {
        const JPPart* p = part(row);
        return p ? m_config.placementCount(p->id) : 0;
    }
    if (c == kFeeders) {
        const JPPart* p = part(row);
        return p ? m_config.feederCount(p->id) : 0;
    }
    return 0;
}

bool JPPartsTableModel::editable(int, int c) const {
    return c == kName || (c >= kDescription && c <= kFiducialVision && c != kMpn);
}

std::vector<const JPPackage*> JPPartsTableModel::packageChoices() const {
    std::vector<const JPPackage*> out;
    for (const auto& k : m_config.packages()) out.push_back(k.get());
    std::sort(out.begin(), out.end(), [](const JPPackage* a, const JPPackage* b) { return a->id < b->id; });
    return out;
}

std::vector<const JPVisionSettings*> JPPartsTableModel::visionChoices(JPVisionSettings::Kind kind) const {
    std::vector<const JPVisionSettings*> out;
    for (const JPVisionSettings& v : m_config.visionSettings())
        if (v.kind == kind) out.push_back(&v);
    std::sort(out.begin(), out.end(), [](const JPVisionSettings* a, const JPVisionSettings* b) { return a->name < b->name; });
    out.push_back(nullptr);
    return out;
}

std::optional<JComboBox::Search> JPPartsTableModel::choiceSearch(int row, int c) const {
    // Its packages as the library's are wherever one is chosen (JPLibrarySearch).
    if (c == kPackage) {
        JComboBox::Search s = JPLibrarySearch::packages(m_config, choices(row, c));
        s.table = JPChooserTable::packages(m_config, choices(row, c));
        return s;
    }
    return std::nullopt;
}

std::vector<std::string> JPPartsTableModel::choices(int, int c) const {
    std::vector<std::string> out;
    if (c == kPackage)
        for (const JPPackage* k : packageChoices()) out.push_back(k->id);
    if (c == kBottomVision || c == kFiducialVision)
        for (const JPVisionSettings* v :
             visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial))
            out.push_back(v ? v->name : std::string());
    return out;
}

bool JPPartsTableModel::setText(int row, int c, const std::string& text, std::string& error) {
    JPPart* p = part(row);
    if (!p) return false;
    switch (c) {
        case kName: {
            // Unique, and never none: what the part is told apart by where it is shown.
            std::string name = text;
            while (!name.empty() && std::isspace(uint8_t(name.front()))) name.erase(name.begin());
            while (!name.empty() && std::isspace(uint8_t(name.back()))) name.pop_back();
            if (name.empty()) {
                error = "A part needs a name.";
                return false;
            }
            const JPPart* named = m_config.partNamed(name);
            const JPPart* byId = m_config.part(name);
            if ((named && named != p) || (byId && byId != p)) {
                error = "A part named " + name + " already exists.";
                return false;
            }
            p->label = name == p->id ? std::string() : name;   // its id again: no name of its own
            break;
        }
        case kDescription:
            p->name = text;
            break;
        case kValue:
            p->value = text;
            break;
        case kHeight:
        case kDepth: {
            JPLength& l = c == kHeight ? p->height : p->throughBoardDepth;
            if (!JPLengthCell::parse(text, l, l)) {
                error = "'" + text + "' is not a length";
                return false;
            }
            break;
        }
        case kSpeed: {
            std::string t;
            for (const char ch : text)
                if (ch != '%') t += ch;
            char* end = nullptr;
            const double v = std::strtod(t.c_str(), &end);
            if (end == t.c_str()) {
                error = "'" + text + "' is not a percentage";
                return false;
            }
            p->speed = v * 0.01;
            break;
        }
        default: return false;
    }
    m_said.changed(onChanged);
    return true;
}

void JPPartsTableModel::setChoice(int row, int c, int index) {
    JPPart* p = part(row);
    if (!p || index < 0) return;
    if (c == kPackage) {
        const auto ks = packageChoices();
        if (size_t(index) < ks.size()) p->packageId = ks[size_t(index)]->id;
    } else if (c == kBottomVision || c == kFiducialVision) {
        const auto vs = visionChoices(c == kBottomVision ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial);
        if (size_t(index) >= vs.size()) return;
        (c == kBottomVision ? p->bottomVisionId : p->fiducialVisionId) = vs[size_t(index)] ? vs[size_t(index)]->id : std::string();
    } else {
        return;
    }
    m_said.changed(onChanged);
}

} // inline namespace jf

inline namespace jf {

const JPFieldValues* JPPartsTableModel::custom(int row) const {
    const JPPart* p = part(row);
    return p ? &p->custom : nullptr;
}

bool JPPartsTableModel::setCustom(int row, const std::string& key, const JJson& value) {
    JPPart* p = part(row);
    if (!p) return false;
    p->custom.set(key, value);
    m_said.changed(onChanged);
    return true;
}

} // inline namespace jf
