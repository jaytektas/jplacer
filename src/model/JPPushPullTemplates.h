// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"
#include "JPLocation.h"

#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// What OpenPnP's ReferencePushPullFeeder does across feeders: templates
// (the feeder whose settings another takes, ranked by its part's tape and
// reel specification or package, being marked a template, feed pitch, tape
// width, part pitch, being in the same row, being on, then nearness), cloning
// a template's settings with its places moved to the other's tape (its frame,
// JPFeederTape), swapping two feeders' places, a new feeder at a place or
// the next in a row; its OCR region; and naming the part OCR read.
class JPPushPullTemplates {
public:
    // What a clone takes: the pick location's Z and options, the tape, the push-pull motion, the vision settings, the pipeline.
    struct Clone {
        bool location = false, tape = false, pushPull = false, vision = false, pipeline = false;
    };
    // OpenPnP's RegionOfInterest: three corners about the camera's centre, and (optionally) where it is from the vision location.
    struct OcrRegion {
        JPLocation                upperLeft { JPLengthUnit::Millimeters }, upperRight { JPLengthUnit::Millimeters },
            lowerLeft { JPLengthUnit::Millimeters };
        bool                      rectify = false;
        std::optional<JPLocation> offsets;
    };

    // The same package, or the same (set) tape and reel specification.
    static bool   compatibleParts(const JPConfiguration& config, const std::string& partId1, const std::string& partId2);
    // EIA-481's tape width from where hole 1 is in the tape's frame.
    static double tapeWidthMm(const JPFeeder& feeder);
    // The template for a feeder (for a part compatible with `compatiblePartId`, else any); empty: none.
    static std::string templateFeeder(const JPConfiguration& config, const std::string& feederId,
                                      const std::string& compatiblePartId = {});
    // The other push-pull feeders whose parts are compatible with this one's.
    static std::vector<std::string> compatibleFeeders(const JPConfiguration& config, const std::string& feederId);
    // OpenPnP's Template status: what a template clones to, or the template a feeder would clone from.
    static std::string cloneTemplateStatus(const JPConfiguration& config, const std::string& feederId);

    static void cloneSettings(JPConfiguration& config, const std::string& feederId, const std::string& templateId, const Clone& what);
    // From its template (for `compatiblePartId`); false (and why) when there is none.
    static bool smartClone(JPConfiguration& config, const std::string& feederId, const std::string& compatiblePartId, const Clone& what,
                           std::string& why);
    // Two feeders' places swapped (each moved to the other's tape).
    static void swapOut(JPConfiguration& config, const std::string& feederId1, const std::string& feederId2);
    // A new push-pull feeder for `partId` (none: the first part) with `templateId`'s places moved to `transform`, on as
    // `enabledAs` is; its id.
    static std::string createAt(JPConfiguration& config, const JPLocation& transform, const std::string& partId,
                                const std::string& templateId, bool enabledAs);
    // OpenPnP's createNewInRow: the next feeder along the row this one makes with the nearest other (else a tape width
    // and 8 mm down the tape), with all its settings; false (and why) when they make no row along X or Y.
    static bool createInRow(JPConfiguration& config, const std::string& feederId, std::string& newId, std::string& why);
    // OpenPnP's setOcrDetectedPart: the part OCR read set (a template only to a compatible part), cloned from its template when `clone`.
    static bool setOcrDetectedPart(JPConfiguration& config, const std::string& feederId, const std::string& partId, bool clone,
                                   std::string& why);

    static std::optional<OcrRegion> ocrRegion(const JPFeeder& feeder);
    static void                     setOcrRegion(JPFeeder& feeder, const std::optional<OcrRegion>& region);

    // OpenPnP's OcrUtils.identifyDetectedPart: the part OCR's text names (its first line; up to a space unless a part id
    // has one; whole, else as the end of one part id after a "-"); false (and why) when none or more than one.
    static bool identifyPart(const JPConfiguration& config, const std::string& ocrText, double avgScore, const std::string& feederName,
                             std::string& partId, std::string& why);
    // OpenPnP's OcrUtils.getConsolidatedPartsAlphabet: every character of the part ids and `stock`, but spaces.
    static std::string partsAlphabet(const JPConfiguration& config, const std::string& stock);
};

} // inline namespace jf
