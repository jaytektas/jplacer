// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStarterLibrary.h"

#include "JPFootprintMaker.h"

#include <algorithm>
#include <vector>

inline namespace jf {

namespace {

using Names = std::vector<const char*>;

void addPackage(JPPartsStore& store, const char* name, JPFootprint f, double height, const Names& names) {
    JPPackage k;
    k.name = name;
    k.length = std::max(f.bodyWidth, f.bodyLength);
    k.width = std::min(f.bodyWidth, f.bodyLength);
    k.height = height;
    f.name = name;
    k.footprintId = store.add(std::move(f));
    const std::string id = store.add(std::move(k));
    store.addName(id, name);
    for (const char* n : names) store.addName(id, n);
}

// A two-terminal chip lying along X, pin 1 on the left.
JPFootprint chip(double padCentre, double padX, double padY, double bodyX, double bodyY) {
    JPFootprintMaker::Dual d;
    d.pins = 2;
    d.pitch = 0;
    d.span = padCentre * 2;
    d.padLength = padX;
    d.padWidth = padY;
    d.bodyWidth = bodyX;
    d.bodyLength = bodyY;
    return JPFootprintMaker::dual("", d);
}

// SOT-23 family: three pads down the left, the rest up the right, at 0.95 mm.
JPFootprint sot23(int pins) {
    JPFootprint f;
    f.pin1 = "1";
    f.bodyWidth = 1.6;
    f.bodyLength = 2.9;
    const double x = 1.1375, w = 1.325, h = 0.6;
    if (pins == 3) {
        f.pads = { { "1", -x, 0.95, w, h, 0, 0 }, { "2", -x, -0.95, w, h, 0, 0 }, { "3", x, 0, w, h, 0, 0 } };
        f.bodyWidth = 1.3;
        return f;
    }
    const double left[3] = { 0.95, 0, -0.95 };
    for (int i = 0; i < 3; ++i) f.pads.push_back({ std::to_string(i + 1), -x, left[i], w, h, 0, 0 });
    const std::vector<double> right = pins == 5 ? std::vector<double>{ -0.95, 0.95 } : std::vector<double>{ -0.95, 0, 0.95 };
    for (size_t i = 0; i < right.size(); ++i) f.pads.push_back({ std::to_string(4 + i), x, right[i], w, h, 0, 0 });
    return f;
}

JPFootprint sot223() {
    JPFootprint f;
    f.pin1 = "1";
    f.bodyWidth = 3.5;
    f.bodyLength = 6.5;
    for (int i = 0; i < 3; ++i) f.pads.push_back({ std::to_string(i + 1), -3.15, 2.3 - i * 2.3, 2.0, 1.5, 0, 0 });
    f.pads.push_back({ "4", 3.15, 0, 2.0, 3.8, 0, 0 });
    return f;
}

JPFootprint dual(int pins, double pitch, double span, double padX, double padY, double bodyX, double bodyY) {
    JPFootprintMaker::Dual d { pins, pitch, span, padX, padY, bodyX, bodyY };
    return JPFootprintMaker::dual("", d);
}

JPFootprint quad(int perSide, double pitch, double span, double padLength, double padWidth, double body, double ep) {
    JPFootprintMaker::Quad q { perSide, pitch, span, padLength, padWidth, body, ep };
    return JPFootprintMaker::quad("", q);
}

} // namespace

void JPStarterLibrary::fill(JPPartsStore& s) {
    // Chips: pad centre, pad X and Y, body X and Y; height a typical part's.
    struct Chip { const char* imperial; const char* metric; double c, px, py, bx, by, h; };
    const Chip chips[] = {
        { "01005", "0402", 0.20, 0.20, 0.23, 0.40, 0.20, 0.13 },
        { "0201", "0603", 0.32, 0.46, 0.40, 0.60, 0.30, 0.26 },
        { "0402", "1005", 0.485, 0.59, 0.64, 1.00, 0.50, 0.35 },
        { "0603", "1608", 0.825, 0.80, 0.95, 1.60, 0.80, 0.45 },
        { "0805", "2012", 0.9125, 1.025, 1.40, 2.00, 1.25, 0.60 },
        { "1206", "3216", 1.4625, 1.125, 1.75, 3.20, 1.60, 0.60 },
        { "1210", "3225", 1.4625, 1.125, 2.65, 3.20, 2.50, 0.60 },
        { "2010", "5025", 2.30, 1.15, 2.60, 5.00, 2.50, 0.60 },
        { "2512", "6332", 2.80, 1.40, 3.35, 6.30, 3.20, 0.60 },
    };
    for (const Chip& c : chips) {
        const std::string i = c.imperial, m = c.metric;
        const std::string kicadTail = "_" + i + "_" + m + "Metric";
        addPackage(s, c.imperial, chip(c.c, c.px, c.py, c.bx, c.by), c.h, {});
        const std::string id = s.packages.back().id;
        for (const std::string& n : { "R" + i, "C" + i, "L" + i, "LED" + i, "D" + i, "R_" + i, "C_" + i,
                                      "R" + kicadTail, "C" + kicadTail, "L" + kicadTail, "LED" + kicadTail,
                                      "D" + kicadTail })
            s.addName(id, n);
    }

    addPackage(s, "SOT-23", sot23(3), 1.1, { "SOT-23-3", "SOT23", "SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR" });
    addPackage(s, "SOT-23-5", sot23(5), 1.1, { "SOT23-5", "SOT-25", "SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BL" });
    addPackage(s, "SOT-23-6", sot23(6), 1.1, { "SOT23-6", "SOT-26", "SOT-23-6_L2.9-W1.6-P0.95-LS2.8-BL" });
    addPackage(s, "SOT-223", sot223(), 1.8, { "SOT-223-3_TabPin2", "SOT-223-3_L6.5-W3.4-P2.30-LS7.0-BR" });

    addPackage(s, "SOD-123", chip(1.65, 0.9, 1.2, 2.7, 1.6), 1.35, { "D_SOD-123", "SOD-123_L2.8-W1.8-LS3.7-RD" });
    addPackage(s, "SOD-323", chip(1.05, 0.6, 0.45, 1.7, 1.25), 1.1, { "D_SOD-323", "SOD-323_L1.8-W1.3-LS2.5-RD" });
    addPackage(s, "SMA", chip(2.0, 2.5, 1.7, 4.3, 2.6), 2.3, { "DO-214AC", "D_SMA", "SMA_L4.4-W2.6-LS5.0-RD" });
    addPackage(s, "SMB", chip(2.15, 2.5, 2.3, 4.3, 3.6), 2.3, { "DO-214AA", "D_SMB", "SMB_L4.6-W3.6-LS5.3-RD" });
    addPackage(s, "SMC", chip(3.4, 3.3, 3.1, 6.9, 5.9), 2.3, { "DO-214AB", "D_SMC", "SMC_L6.9-W5.9-LS7.9-RD" });

    // Dual rows: name, pins, pitch, pad centres across, pad X and Y, body X and Y, height, other names.
    struct Row { const char* name; int pins; double pitch, span, px, py, bx, by, h; Names names; };
    const Row duals[] = {
        { "SOIC-8", 8, 1.27, 4.95, 1.95, 0.6, 3.9, 4.9, 1.75, { "SOIC-8_3.9x4.9mm_P1.27mm", "SOIC-8_L4.9-W3.9-P1.27-LS6.0-BL", "SOP-8" } },
        { "SOIC-14", 14, 1.27, 4.95, 1.95, 0.6, 3.9, 8.65, 1.75, { "SOIC-14_3.9x8.7mm_P1.27mm", "SOIC-14_L8.7-W3.9-P1.27-LS6.0-BL", "SOP-14" } },
        { "SOIC-16", 16, 1.27, 4.95, 1.95, 0.6, 3.9, 9.9, 1.75, { "SOIC-16_3.9x9.9mm_P1.27mm", "SOIC-16_L10.0-W3.9-P1.27-LS6.0-BL", "SOP-16" } },
        { "SOIC-16W", 16, 1.27, 9.3, 2.0, 0.6, 7.5, 10.3, 2.65, { "SOIC-16W_7.5x10.3mm_P1.27mm", "SOIC-16_L10.3-W7.5-P1.27-LS10.3-BL" } },
        { "SOIC-20W", 20, 1.27, 9.3, 2.0, 0.6, 7.5, 12.8, 2.65, { "SOIC-20W_7.5x12.8mm_P1.27mm", "SOIC-20_L12.8-W7.5-P1.27-LS10.3-BL" } },
        { "SOIC-24W", 24, 1.27, 9.3, 2.0, 0.6, 7.5, 15.4, 2.65, { "SOIC-24W_7.5x15.4mm_P1.27mm", "SOIC-24_L15.4-W7.5-P1.27-LS10.3-BL" } },
        { "SOIC-28W", 28, 1.27, 9.3, 2.0, 0.6, 7.5, 17.9, 2.65, { "SOIC-28W_7.5x17.9mm_P1.27mm", "SOIC-28_L17.9-W7.5-P1.27-LS10.3-BL" } },
        { "TSSOP-8", 8, 0.65, 5.725, 1.525, 0.45, 4.4, 3.0, 1.2, { "TSSOP-8_4.4x3mm_P0.65mm", "TSSOP-8_L3.0-W4.4-P0.65-LS6.4-BL" } },
        { "TSSOP-14", 14, 0.65, 5.725, 1.525, 0.45, 4.4, 5.0, 1.2, { "TSSOP-14_4.4x5mm_P0.65mm", "TSSOP-14_L5.0-W4.4-P0.65-LS6.4-BL" } },
        { "TSSOP-16", 16, 0.65, 5.725, 1.525, 0.45, 4.4, 5.0, 1.2, { "TSSOP-16_4.4x5mm_P0.65mm", "TSSOP-16_L5.0-W4.4-P0.65-LS6.4-BL" } },
        { "TSSOP-20", 20, 0.65, 5.725, 1.525, 0.45, 4.4, 6.5, 1.2, { "TSSOP-20_4.4x6.5mm_P0.65mm", "TSSOP-20_L6.5-W4.4-P0.65-LS6.4-BL" } },
        { "TSSOP-24", 24, 0.65, 5.725, 1.525, 0.45, 4.4, 7.8, 1.2, { "TSSOP-24_4.4x7.8mm_P0.65mm", "TSSOP-24_L7.8-W4.4-P0.65-LS6.4-BL" } },
        { "TSSOP-28", 28, 0.65, 5.725, 1.525, 0.45, 4.4, 9.7, 1.2, { "TSSOP-28_4.4x9.7mm_P0.65mm", "TSSOP-28_L9.7-W4.4-P0.65-LS6.4-BL" } },
    };
    for (const Row& r : duals) {
        addPackage(s, r.name, dual(r.pins, r.pitch, r.span, r.px, r.py, r.bx, r.by), r.h, r.names);
    }
    // Quads: name, pins per side, pitch, pad centres across, pad length and width, body, exposed pad, height, other names.
    struct Sq { const char* name; int perSide; double pitch, span, pl, pw, body, ep, h; Names names; };
    const Sq quads[] = {
        { "QFN-16_3x3", 4, 0.5, 2.95, 0.85, 0.25, 3, 1.7, 0.9, { "QFN-16-1EP_3x3mm_P0.5mm_EP1.7x1.7mm", "QFN-16_L3.0-W3.0-P0.50-BL-EP1.7" } },
        { "QFN-20_4x4", 5, 0.5, 3.95, 0.85, 0.25, 4, 2.6, 0.9, { "QFN-20-1EP_4x4mm_P0.5mm_EP2.6x2.6mm", "QFN-20_L4.0-W4.0-P0.50-BL-EP2.6" } },
        { "QFN-24_4x4", 6, 0.5, 3.95, 0.85, 0.25, 4, 2.6, 0.9, { "QFN-24-1EP_4x4mm_P0.5mm_EP2.6x2.6mm", "QFN-24_L4.0-W4.0-P0.50-BL-EP2.6" } },
        { "QFN-32_5x5", 8, 0.5, 4.95, 0.85, 0.25, 5, 3.45, 0.9, { "QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm", "QFN-32_L5.0-W5.0-P0.50-BL-EP3.5" } },
        { "QFN-40_6x6", 10, 0.5, 5.95, 0.85, 0.25, 6, 4.6, 0.9, { "QFN-40-1EP_6x6mm_P0.5mm_EP4.6x4.6mm", "QFN-40_L6.0-W6.0-P0.50-BL-EP4.6" } },
        { "QFN-48_7x7", 12, 0.5, 6.95, 0.85, 0.25, 7, 5.6, 0.9, { "QFN-48-1EP_7x7mm_P0.5mm_EP5.6x5.6mm", "QFN-48_L7.0-W7.0-P0.50-BL-EP5.6" } },
        { "LQFP-32", 8, 0.8, 8.3, 1.5, 0.55, 7, 0, 1.6, { "LQFP-32_7x7mm_P0.8mm", "LQFP-32_L7.0-W7.0-P0.80-LS9.0-BL" } },
        { "LQFP-44", 11, 0.8, 11.3, 1.5, 0.55, 10, 0, 1.6, { "LQFP-44_10x10mm_P0.8mm", "LQFP-44_L10.0-W10.0-P0.80-LS12.0-BL" } },
        { "LQFP-48", 12, 0.5, 8.3, 1.5, 0.3, 7, 0, 1.6, { "LQFP-48_7x7mm_P0.5mm", "LQFP-48_L7.0-W7.0-P0.50-LS9.0-BL" } },
        { "LQFP-64", 16, 0.5, 11.3, 1.5, 0.3, 10, 0, 1.6, { "LQFP-64_10x10mm_P0.5mm", "LQFP-64_L10.0-W10.0-P0.50-LS12.0-BL" } },
        { "LQFP-100", 25, 0.5, 15.3, 1.5, 0.3, 14, 0, 1.6, { "LQFP-100_14x14mm_P0.5mm", "LQFP-100_L14.0-W14.0-P0.50-LS16.0-BL" } },
        { "LQFP-144", 36, 0.5, 21.3, 1.5, 0.3, 20, 0, 1.6, { "LQFP-144_20x20mm_P0.5mm", "LQFP-144_L20.0-W20.0-P0.50-LS22.0-BL" } },
    };
    for (const Sq& q : quads) {
        addPackage(s, q.name, quad(q.perSide, q.pitch, q.span, q.pl, q.pw, q.body, q.ep), q.h, q.names);
    }
}

} // inline namespace jf
