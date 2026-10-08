// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPLength.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A part, as OpenPnP's Part: its id, name, height (and the depth it reaches
// through the board), its package (by id), speed (a share of full),
// pick retries, and the vision settings it uses (by id; empty: its
// package's).
class JPPart {
public:
    // A typed, exact name for it (DESIGN.md, Identifier): kind "mpn" (org the manufacturer) or "supplierPn"
    // (org the supplier); several of each (second sources, alternates).
    struct Identifier {
        std::string kind;
        std::string org;
        std::string code;
    };
    // What a CAD file or BOM may call it (DESIGN.md, AKA): `text` as the `field` gives it ("value", "footprint",
    // or "valueFootprint": "value|footprint"), and where it was learned (a board, when).
    struct Aka {
        std::string field;
        std::string text;
        std::string learnedFrom;
        std::string when;
    };

    // One way the part comes (DESIGN.md, Packaging): cut tape, a reel, a tray, a tube, loose. Tape's width,
    // the pitch between pockets, paper or embossed, and the part's rotation as it sits in its packaging (set
    // once here, not on each feeder); how many a reel, tray or tube holds (0: not known).
    struct Packaging {
        std::string kind = "Cut tape";   // kPackagingKinds
        double      tapeWidthMm = 8;
        double      pitchMm = 4;
        std::string tapeType = "Paper";  // "Paper" or "Embossed"
        double      rotationDeg = 0;
        int         quantity = 0;
        std::string note;
    };
    static constexpr const char* kPackagingKinds[] = { "Cut tape", "Reel", "Tray", "Tube", "Loose" };
    // Where it is bought (DESIGN.md, Supplier offer): the supplier and their part number (SKU), the packaging
    // it comes in, the least they sell, their price breaks ("1: 0.0100, 100: 0.0050"), a link, and the last
    // price seen and when.
    struct Offer {
        std::string supplier;
        std::string sku;
        std::string packaging;
        int         moq = 0;
        std::string priceBreaks;
        std::string link;
        std::string lastPrice;
        std::string lastWhen;
    };

    std::string                id;          // unique in the library; what placements, feeders and jobs name it by
    std::string                uuid;        // the library's for good (JPUuid); empty until it is in the library
    std::string                value;       // its electrical value as written ("100n"), where it has one
    std::string                datasheet;   // a link or a file
    std::vector<Identifier>    identifiers;
    std::vector<Aka>           akas;
    std::vector<Packaging>     packagings;
    std::vector<Offer>         offers;
    std::optional<std::string> name;
    JPLength                   height { 0, JPLengthUnit::Millimeters };
    JPLength                   throughBoardDepth { 0, JPLengthUnit::Millimeters };
    std::string                packageId;
    double                     speed = 1.0;
    int                        pickRetryCount = 0;
    std::string                bottomVisionId;
    std::string                fiducialVisionId;

    // Height plus the depth through the board: what hangs below a nozzle.
    JPLength heightForSafeZ() const { return height.add(throughBoardDepth); }
    bool     isPartHeightUnknown() const { return height.value() <= 0.0; }

    static JPPart fromXml(const JPXmlElement& e);
    JPXmlNode toXml() const;
};

} // inline namespace jf
