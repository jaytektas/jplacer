// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPEntryFields.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>

inline namespace jf {

namespace {

using K = JPEntry::Kind;

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

bool toNumber(const std::string& text, double& out) {
    const std::string t = trim(text);
    if (t.empty()) {
        out = 0;
        return true;
    }
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && trim(end).empty();
}

std::vector<std::string> split(const std::string& text, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at <= text.size()) {
        const size_t next = text.find(sep, at);
        const std::string item = trim(text.substr(at, next == std::string::npos ? std::string::npos : next - at));
        if (!item.empty()) out.push_back(item);
        if (next == std::string::npos) break;
        at = next + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (const std::string& s : v) out += (out.empty() ? "" : sep) + s;
    return out;
}

std::string numbersText(const std::vector<JPSupplierNumber>& ns) {
    std::vector<std::string> parts;
    for (const JPSupplierNumber& n : ns) parts.push_back(n.supplier.empty() ? n.number : n.supplier + " " + n.number);
    return join(parts, "; ");
}

// "LCSC C17408; Digi-Key 311-100CRCT-ND; C25804": a supplier, then its number
// (the last word); a number alone has no supplier.
std::vector<JPSupplierNumber> numbersOf(const std::string& text) {
    std::vector<JPSupplierNumber> out;
    for (const std::string& item : split(text, ';')) {
        const size_t sp = item.find_last_of(' ');
        if (sp == std::string::npos) out.push_back({ "", item });
        else out.push_back({ trim(item.substr(0, sp)), trim(item.substr(sp + 1)) });
    }
    return out;
}

} // namespace

const std::vector<JPEntryFields::Field>& JPEntryFields::of(JPEntry::Kind kind) {
    static const std::vector<Field> part = {
        { "mpn", "MPN" }, { "manufacturer", "Manufacturer" }, { "value", "Value" }, { "tolerance", "Tolerance" },
        { "voltage", "Voltage" }, { "power", "Power" }, { "dielectric", "Dielectric" }, { "temperature", "Temperature" },
        { "numbers", "Supplier numbers" }, { "description", "Description" }, { "package", "Package" },
    };
    static const std::vector<Field> package = {
        { "name", "Name" }, { "length", "Length (mm)" }, { "width", "Width (mm)" }, { "height", "Height (mm)" },
        { "tips", "Nozzle tips" }, { "speed", "Speed (%)" }, { "pickRetries", "Pick retries" },
        { "turn", "Turn (\xC2\xB0)" }, { "names", "Names" }, { "footprint", "Footprint" },
    };
    static const std::vector<Field> footprint = {
        { "name", "Name" }, { "bodyWidth", "Body X (mm)" }, { "bodyLength", "Body Y (mm)" }, { "pin1", "Pin 1" },
        { "pads", "Pads" },
    };
    return kind == K::Part ? part : kind == K::Package ? package : footprint;
}

std::string JPEntryFields::get(const JPPartsStore& s, const JPEntry& e, const std::string& key) {
    if (e.kind == K::Part) {
        const JPPart* p = s.part(e.id);
        if (!p) return {};
        if (key == "mpn") return p->mpn;
        if (key == "manufacturer") return p->manufacturer;
        if (key == "value") return p->value;
        if (key == "tolerance") return p->tolerance;
        if (key == "voltage") return p->voltage;
        if (key == "power") return p->power;
        if (key == "dielectric") return p->dielectric;
        if (key == "temperature") return p->temperature;
        if (key == "numbers") return numbersText(p->supplierNumbers);
        if (key == "description") return p->description;
        if (key == "package") return s.package(p->packageId) ? s.package(p->packageId)->name : std::string();
        return {};
    }
    if (e.kind == K::Package) {
        const JPPackage* k = s.package(e.id);
        if (!k) return {};
        if (key == "name") return k->name;
        if (key == "length") return k->length > 0 ? number(k->length) : std::string();
        if (key == "width") return k->width > 0 ? number(k->width) : std::string();
        if (key == "height") return k->height > 0 ? number(k->height) : std::string();
        if (key == "tips") return join(k->tips, ", ");
        if (key == "speed") return k->speed > 0 ? number(k->speed * 100) : std::string();
        if (key == "pickRetries") return k->pickRetries >= 0 ? std::to_string(k->pickRetries) : std::string();
        if (key == "turn") return number(k->turnDeg);
        if (key == "names") return join(k->names, ", ");
        if (key == "footprint") return s.footprint(k->footprintId) ? s.footprint(k->footprintId)->name : std::string();
        return {};
    }
    const JPFootprint* f = s.footprint(e.id);
    if (!f) return {};
    if (key == "name") return f->name;
    if (key == "bodyWidth") return f->bodyWidth > 0 ? number(f->bodyWidth) : std::string();
    if (key == "bodyLength") return f->bodyLength > 0 ? number(f->bodyLength) : std::string();
    if (key == "pin1") return f->pin1;
    if (key == "pads") return std::to_string(f->pads.size());
    return {};
}

bool JPEntryFields::set(JPPartsStore& s, const JPEntry& e, const std::string& key, const std::string& textIn,
                        std::string& error) {
    const std::string text = trim(textIn);
    if (get(s, e, key) == text) return true;
    auto length = [&](double& field) {
        double v;
        if (!toNumber(text, v) || v < 0) {
            error = "'" + text + "' is not a length in millimetres";
            return false;
        }
        field = v;
        return true;
    };
    if (e.kind == K::Part) {
        JPPart* p = s.part(e.id);
        if (!p) return false;
        std::string* fields[] = { &p->mpn, &p->manufacturer, &p->value, &p->tolerance, &p->voltage, &p->power,
                                  &p->dielectric, &p->temperature, &p->description };
        const char* keys[] = { "mpn", "manufacturer", "value", "tolerance", "voltage", "power", "dielectric",
                               "temperature", "description" };
        bool done = false;
        for (size_t i = 0; i < std::size(keys); ++i)
            if (key == keys[i]) {
                *fields[i] = text;
                done = true;
            }
        if (key == "numbers") {
            p->supplierNumbers = numbersOf(text);
            done = true;
        }
        if (!done) {
            error = "the " + key + " is changed by choosing another";
            return false;
        }
        ++p->revision;
        return true;
    }
    if (e.kind == K::Package) {
        JPPackage* k = s.package(e.id);
        if (!k) return false;
        if (key == "name") {
            if (text.empty()) {
                error = "a package needs a name";
                return false;
            }
            k->name = text;
        } else if (key == "length") {
            if (!length(k->length)) return false;
        } else if (key == "width") {
            if (!length(k->width)) return false;
        } else if (key == "height") {
            if (!length(k->height)) return false;
        } else if (key == "tips") {
            k->tips = split(text, ',');
        } else if (key == "speed") {
            double v;
            if (!toNumber(text, v) || v < 0 || v > 100) {
                error = "the speed is a percentage of the machine's, 1 to 100 (empty: the machine's)";
                return false;
            }
            k->speed = v / 100;
        } else if (key == "pickRetries") {
            double v;
            if (!toNumber(text, v) || v < 0 || v != double(int(v))) {
                error = "pick retries is a whole number (empty: the machine's)";
                return false;
            }
            k->pickRetries = text.empty() ? -1 : int(v);
        } else if (key == "turn") {
            double v;
            if (!toNumber(text, v)) {
                error = "'" + text + "' is not an angle in degrees";
                return false;
            }
            k->turnDeg = v;
        } else if (key == "names") {
            const std::vector<std::string> wanted = split(text, ',');
            for (const std::string& n : wanted)
                if (const JPPackage* owner = s.packageNamed(n); owner && owner != k) {
                    error = "'" + n + "' is " + owner->name + "'s: a name belongs to one package";
                    return false;
                }
            k->names = wanted;
        } else {
            error = "the " + key + " is changed by choosing another";
            return false;
        }
        ++k->revision;
        return true;
    }
    JPFootprint* f = s.footprint(e.id);
    if (!f) return false;
    if (key == "name") {
        if (text.empty()) {
            error = "a footprint needs a name";
            return false;
        }
        f->name = text;
    } else if (key == "bodyWidth") {
        if (!length(f->bodyWidth)) return false;
    } else if (key == "bodyLength") {
        if (!length(f->bodyLength)) return false;
    } else if (key == "pin1") {
        if (!text.empty() && !f->pad(text)) {
            error = "there is no pad '" + text + "'";
            return false;
        }
        f->pin1 = text;
    } else {
        error = "the " + key + " is read from the footprint";
        return false;
    }
    ++f->revision;
    return true;
}

std::vector<std::string> JPEntryFields::differences(const JPPartsStore& a, const JPEntry& ea, const JPPartsStore& b,
                                                    const JPEntry& eb) {
    std::vector<std::string> out;
    for (const Field& f : of(ea.kind)) {
        const std::string va = get(a, ea, f.key), vb = get(b, eb, f.key);
        if (va != vb) out.push_back(f.label + ": " + (va.empty() ? "(none)" : va) + " \xE2\x86\x92 " + (vb.empty() ? "(none)" : vb));
    }
    if (ea.kind == K::Footprint) {
        const JPFootprint* fa = a.footprint(ea.id);
        const JPFootprint* fb = b.footprint(eb.id);
        if (fa && fb && fa->pads.size() == fb->pads.size() && fa->pads != fb->pads) out.push_back("Pads: moved or resized");
    }
    return out;
}

} // inline namespace jf
