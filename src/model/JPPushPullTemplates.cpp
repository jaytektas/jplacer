// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPushPullTemplates.h"

#include "JPFeederTape.h"
#include "JPLocationXml.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
constexpr const char* kPushPull = "ReferencePushPullFeeder";
// OpenPnP's: how close two feeders' pick locations are to be in a row (mm), in Z too.
constexpr double kRowToleranceMm = 4.0, kRowZToleranceMm = 1.0;
// EIA-481: a part's middle is half the tape width less this from its hole's; the row of 3D printed feeders this
// feeder was made with is the tape width and this apart.
constexpr double kHalfTapeWidthDiffMm = 0.5, kRowGapMm = 8.0;
// A rotation of more than this between two tapes swaps a lever's X and Y.
constexpr double kRotatedDegrees = 45;
// Tape widths and pitches the same to this.
constexpr double kSameTapeMm = 0.01;

// A tape `a` comes on that `b` comes on too (its width, pitch and type), or null.
const JPPart::Packaging* commonTape(const JPPart& a, const JPPart& b) {
    auto taped = [](const JPPart::Packaging& k) { return k.kind == "Cut tape" || k.kind == "Reel"; };
    for (const JPPart::Packaging& x : a.packagings)
        if (taped(x))
            for (const JPPart::Packaging& y : b.packagings)
                if (taped(y) && std::abs(x.tapeWidthMm - y.tapeWidthMm) < kSameTapeMm
                    && std::abs(x.pitchMm - y.pitchMm) < kSameTapeMm && x.tapeType == y.tapeType)
                    return &x;
    return nullptr;
}

// A tape as said: "8 mm Paper, 4 mm pitch".
std::string tapeSaid(const JPPart::Packaging& k) {
    auto n = [](double v) {
        char b[32];
        std::snprintf(b, sizeof b, "%g", v);
        return std::string(b);
    };
    return n(k.tapeWidthMm) + " mm " + k.tapeType + ", " + n(k.pitchMm) + " mm pitch";
}

double mm(const JPLength& l) { return l.convertToUnits(kMm).value(); }

const JPPart* partOf(const JPConfiguration& config, const JPFeeder& f) {
    const std::string id = f.partId();
    return id.empty() ? nullptr : config.part(id);
}

std::vector<const JPFeeder*> pushPulls(const JPConfiguration& config) {
    std::vector<const JPFeeder*> out;
    for (const JPFeeder& f : config.feeders())
        if (f.typeName() == kPushPull) out.push_back(&f);
    return out;
}

bool onSameRow(const JPFeeder& a, const JPFeeder& b) {
    const JPLocation d = a.location().convertToUnits(kMm).subtract(b.location());
    return std::abs(d.z()) < kRowZToleranceMm && (std::abs(d.x()) < kRowToleranceMm || std::abs(d.y()) < kRowToleranceMm);
}

// FeederVisionHelper's forwardTransform and backwardTransform.
JPLocation forward(const JPLocation& l, const JPLocation& t) { return l.rotateXy(t.rotation()).addWithRotation(t); }
JPLocation backward(const JPLocation& l, const JPLocation& t) { return l.subtractWithRotation(t).rotateXy(-t.rotation()); }

// A place on the old tape to the same place on the new one (an unset place stays unset).
JPLocation relocated(const JPLocation& l, const JPLocation& from, const JPLocation& to) {
    if (!l.isInitialized()) return l;
    return forward(backward(l, from), to);
}
JPLocation relocatedXy(const JPLocation& l, const JPLocation& from, const JPLocation& to) {
    return relocated(l.multiply(1.0, 1.0, 0.0, 0.0), from, to);
}

JPLocation transformOf(const JPFeeder& f) { return JPFeederTape::transform(std::nullopt, JPFeederTape::of(f)); }

// A push-pull feeder's own (another kind: a new one's).
const JPPushPullFeeder& own(const JPFeeder& f) {
    static const JPPushPullFeeder none;
    const JPPushPullFeeder* p = f.as<JPPushPullFeeder>();
    return p ? *p : none;
}

// OpenPnP's setFeederLocation: `templ`'s places (holes and pick location, push-pull places, OCR region) moved from its tape to `to`.
void setFeederLocation(JPFeeder& f, const JPLocation& to, bool primary, bool pushPull, bool vision, const JPFeeder& templ) {
    JPPushPullFeeder* p = f.as<JPPushPullFeeder>();
    if (!p) return;
    const JPPushPullFeeder& t = own(templ);
    const JPLocation from = transformOf(templ);
    if (primary) {
        f.setTapeHole(1, relocatedXy(t.hole1, from, to));
        f.setTapeHole(2, relocatedXy(t.hole2, from, to));
    }
    if (pushPull) {
        // Turned: the lever's X and Y swap.
        const bool rotated = std::abs(std::sin((from.rotation() - to.rotation()) * M_PI / 180)) > std::sin(kRotatedDegrees * M_PI / 180);
        p->calibrateMotionX = rotated ? t.calibrateMotionY : t.calibrateMotionX;
        p->calibrateMotionY = rotated ? t.calibrateMotionX : t.calibrateMotionY;
        p->additiveRotation = t.additiveRotation;
        for (JPLocation JPPushPullFeeder::* place : { &JPPushPullFeeder::feedStart, &JPPushPullFeeder::feedMid1, &JPPushPullFeeder::feedMid2,
                                                      &JPPushPullFeeder::feedMid3, &JPPushPullFeeder::feedEnd })
            p->*place = relocated(t.*place, from, to);
    }
    if (vision)
        if (std::optional<JPPushPullFeeder::OcrRegion> region = t.ocrRegion) {
            const double by = to.rotation() - from.rotation();
            region->upperLeft = region->upperLeft.rotateXy(by);
            region->upperRight = region->upperRight.rotateXy(by);
            region->lowerLeft = region->lowerLeft.rotateXy(by);
            if (region->offsets) region->offsets = region->offsets->rotateXy(by);
            p->ocrRegion = region;
        }
    if (primary) f.setLocation(relocated(templ.location(), from, to));
}

} // namespace

bool JPPushPullTemplates::compatibleParts(const JPConfiguration& config, const std::string& partId1, const std::string& partId2) {
    const JPPart* p1 = partId1.empty() ? nullptr : config.part(partId1);
    const JPPart* p2 = partId2.empty() ? nullptr : config.part(partId2);
    if (!p1 || !p2) return false;
    return p1->packageId == p2->packageId || commonTape(*p1, *p2);
}

double JPPushPullTemplates::tapeWidthMm(const JPFeeder& feeder) {
    const JPFeederTape::Params p = JPFeederTape::of(feeder);
    const JPLocation hole1 = JPFeederTape::machineToFeeder(p.hole1Location, std::nullopt, p).convertToUnits(kMm);
    return std::floor(hole1.y() + kHalfTapeWidthDiffMm + 0.5) * 2;
}

std::string JPPushPullTemplates::templateFeeder(const JPConfiguration& config, const std::string& feederId,
                                                const std::string& compatiblePartId) {
    const JPFeeder* self = config.feeder(feederId);
    if (!self) return {};
    std::vector<const JPFeeder*> list = pushPulls(config);
    const std::string compare = compatiblePartId.empty() ? self->partId() : compatiblePartId;
    const double feedPitch = mm(own(*self).feedPitch);
    const double partPitch = mm(own(*self).partPitch);
    const double width = tapeWidthMm(*self);
    // Ranked by similarity: each test in turn, then nearness.
    auto rank = [&](const JPFeeder* f) {
        return std::array<int, 7> { compatibleParts(config, compare, f->partId()) ? 0 : 1, own(*f).usedAsTemplate ? 0 : 1,
                                    mm(own(*f).feedPitch) == feedPitch ? 0 : 1, tapeWidthMm(*f) == width ? 0 : 1,
                                    mm(own(*f).partPitch) == partPitch ? 0 : 1,
                                    onSameRow(*self, *f) ? 0 : 1, f->enabled() ? 0 : 1 };
    };
    const JPLocation at = self->location().convertToUnits(kMm);
    std::stable_sort(list.begin(), list.end(), [&](const JPFeeder* a, const JPFeeder* b) {
        const auto ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        return at.linearDistanceTo(a->location()) < at.linearDistanceTo(b->location());
    });
    for (const JPFeeder* t : list)
        if (partOf(config, *t) && t->id() != feederId && (compatiblePartId.empty() || compatibleParts(config, compatiblePartId, t->partId())))
            return t->id();
    return {};
}

std::vector<std::string> JPPushPullTemplates::compatibleFeeders(const JPConfiguration& config, const std::string& feederId) {
    std::vector<std::string> out;
    const JPFeeder* self = config.feeder(feederId);
    if (!self) return out;
    for (const JPFeeder* f : pushPulls(config))
        if (partOf(config, *f) && f->id() != feederId && compatibleParts(config, self->partId(), f->partId())) out.push_back(f->id());
    return out;
}

std::string JPPushPullTemplates::cloneTemplateStatus(const JPConfiguration& config, const std::string& feederId) {
    const JPFeeder* self = config.feeder(feederId);
    if (!self) return {};
    std::string status;
    if (own(*self).usedAsTemplate) {
        status = "Clones to ";
        int n = 0;
        for (const std::string& id : compatibleFeeders(config, feederId)) {
            const JPFeeder* t = config.feeder(id);
            status += n++ > 0 ? ",\n" : "\n";
            status += t->name() + " " + t->partId();
        }
        status += n == 0 ? "none." : "\n(Count: " + std::to_string(n) + ")";
        return status;
    }
    const std::string templ = templateFeeder(config, feederId);
    if (templ.empty()) return "None found";
    const JPFeeder* t = config.feeder(templ);
    status = t->name();
    if (partOf(config, *t)) status += " with part " + t->partId();
    const JPPart* mine = partOf(config, *self);
    const JPPart* theirs = partOf(config, *t);
    if (mine && theirs && compatibleParts(config, self->partId(), t->partId())) {
        const JPPackage* pkg = config.package(mine->packageId);
        if (mine->packageId == theirs->packageId && pkg)
            status += " selected by common package " + pkg->shownName();
        else if (const JPPart::Packaging* tape = commonTape(*mine, *theirs))
            status += " selected by common tape " + tapeSaid(*tape);
    } else {
        status += " selected as partial match.\nTheir tapes (the parts' Packaging) and packages differ, settings must be reviewed.";
    }
    return status;
}

void JPPushPullTemplates::cloneSettings(JPConfiguration& config, const std::string& feederId, const std::string& templateId,
                                        const Clone& what) {
    JPFeeder* f = config.feeder(feederId);
    const JPFeeder* tp = config.feeder(templateId);
    if (!f || !tp) return;
    const JPFeeder t = *tp;   // the list may move as f changes
    JPPushPullFeeder* p = f->as<JPPushPullFeeder>();
    const JPPushPullFeeder& o = own(t);
    if (!p) return;
    if (what.location) {
        // Just the pick location's Z, and the options.
        f->setLocation(f->location().derive(t.location(), false, false, true, false));
        p->normalizePickLocation = o.normalizePickLocation;
        p->snapToAxis = o.snapToAxis;
    }
    if (what.tape) {
        p->partPitch = o.partPitch;
        p->rotationInFeeder = o.rotationInFeeder;
        p->feedPitch = o.feedPitch;
        p->feedMultiplier = o.feedMultiplier;
        f->setFeedCount(0);
    }
    if (what.pushPull) {
        p->actuatorName = o.actuatorName;
        p->peelOffActuatorName = o.peelOffActuatorName;
        for (double JPPushPullFeeder::* speed : { &JPPushPullFeeder::feedSpeedPush1, &JPPushPullFeeder::feedSpeedPush2,
                                                  &JPPushPullFeeder::feedSpeedPush3, &JPPushPullFeeder::feedSpeedPushEnd,
                                                  &JPPushPullFeeder::feedSpeedPull3, &JPPushPullFeeder::feedSpeedPull2,
                                                  &JPPushPullFeeder::feedSpeedPull1, &JPPushPullFeeder::feedSpeedPull0 })
            p->*speed = o.*speed;
        for (bool JPPushPullFeeder::* in : { &JPPushPullFeeder::includedPush1, &JPPushPullFeeder::includedPush2, &JPPushPullFeeder::includedPush3,
                                             &JPPushPullFeeder::includedPushEnd, &JPPushPullFeeder::includedPull3, &JPPushPullFeeder::includedPull2,
                                             &JPPushPullFeeder::includedPull1, &JPPushPullFeeder::includedPull0, &JPPushPullFeeder::includedMulti0,
                                             &JPPushPullFeeder::includedMulti1, &JPPushPullFeeder::includedMulti2, &JPPushPullFeeder::includedMulti3,
                                             &JPPushPullFeeder::includedMultiEnd })
            p->*in = o.*in;
        for (int JPPushPullFeeder::* delay : { &JPPushPullFeeder::delay0, &JPPushPullFeeder::delay1, &JPPushPullFeeder::delay2,
                                               &JPPushPullFeeder::delay3, &JPPushPullFeeder::delay4 })
            p->*delay = o.*delay;
    }
    if (what.vision) {
        p->calibrationTrigger = o.calibrationTrigger;
        p->ocrFontName = o.ocrFontName;
        p->ocrFontSizePt = o.ocrFontSizePt;
        p->ocrWrongPartAction = o.ocrWrongPartAction;
        p->ocrDiscoverOnJobStart = o.ocrDiscoverOnJobStart;
        p->ocrStopAfterWrongPart = o.ocrStopAfterWrongPart;
        p->precisionWantedMm = o.precisionWantedMm;
        JPFeederTape::resetCalibrationStatistics(*f);
        f->setFeedCount(0);
    }
    if (what.pipeline) {
        if (o.pipeline) p->pipeline = o.pipeline;
        p->pipelineType = o.pipelineType;
    }
    // The template's places moved to this feeder's tape.
    setFeederLocation(*f, transformOf(*f), false, true, true, t);
}

bool JPPushPullTemplates::smartClone(JPConfiguration& config, const std::string& feederId, const std::string& compatiblePartId,
                                     const Clone& what, std::string& why) {
    const std::string templ = templateFeeder(config, feederId, compatiblePartId);
    if (templ.empty()) {
        const JPFeeder* f = config.feeder(feederId);
        const std::string name = f ? f->name() : feederId;
        why = compatiblePartId.empty() ? "Feeder " + name + ": No suitable template feeder found to clone."
                                       : "Feeder " + name + ": No template feeder found to clone for part " + compatiblePartId + " compatibility.";
        return false;
    }
    cloneSettings(config, feederId, templ, what);
    return true;
}

void JPPushPullTemplates::swapOut(JPConfiguration& config, const std::string& feederId1, const std::string& feederId2) {
    JPFeeder* a = config.feeder(feederId1);
    JPFeeder* b = config.feeder(feederId2);
    if (!a || !b) return;
    const JPFeeder oldA = *a, oldB = *b;
    const JPLocation t1 = transformOf(oldA), t2 = transformOf(oldB);
    setFeederLocation(*a, t2, true, true, true, oldA);
    setFeederLocation(*config.feeder(feederId2), t1, true, true, true, oldB);
}

std::string JPPushPullTemplates::createAt(JPConfiguration& config, const JPLocation& transform, const std::string& partId,
                                          const std::string& templateId, bool enabledAs) {
    const JPFeeder* tp = config.feeder(templateId);
    if (!tp) return {};
    const JPFeeder t = *tp;
    std::string part = partId;
    if (part.empty() && !config.parts().empty()) part = config.parts().front()->id;
    JPFeeder fresh = JPFeeder::create(std::string("org.openpnp.machine.reference.feeder.") + kPushPull, part);
    const std::string id = fresh.id();
    setFeederLocation(fresh, transform, true, true, true, t);
    fresh.setEnabled(enabledAs);
    config.addFeeder(std::move(fresh));
    return id;
}

bool JPPushPullTemplates::createInRow(JPConfiguration& config, const std::string& feederId, std::string& newId, std::string& why) {
    const JPFeeder* self = config.feeder(feederId);
    if (!self) return false;
    const JPLocation at = self->location().convertToUnits(kMm);
    // The nearest other with a part.
    const JPFeeder* closest = nullptr;
    double best = INFINITY;
    for (const JPFeeder* f : pushPulls(config))
        if (f->id() != feederId && partOf(config, *f))
            if (const double d = at.linearDistanceTo(f->location()); !closest || d < best) {
                best = d;
                closest = f;
            }
    // A row of the feeders this was made with: a tape width and 8 mm down the tape; else the row with the nearest.
    const JPFeederTape::Params p = JPFeederTape::of(*self);
    JPLocation unit = JPFeederTape::feederToMachine(JPLocation(kMm, 0, -tapeWidthMm(*self) - kRowGapMm, 0, 0), std::nullopt, p)
                          .convertToUnits(kMm)
                          .subtract(at);
    if (closest) {
        unit = at.subtract(closest->location()).convertToUnits(kMm);
        if (own(*self).snapToAxis) {
            if (std::abs(unit.x()) > kRowToleranceMm && std::abs(unit.y()) <= kRowToleranceMm) unit = unit.multiply(1.0, 0.0, 0.0, 0.0);
            else if (std::abs(unit.y()) > kRowToleranceMm && std::abs(unit.x()) <= kRowToleranceMm) unit = unit.multiply(0.0, 1.0, 0.0, 0.0);
            else {
                why = "Closest feeder " + closest->name() + " " + closest->partId() + " does not form a row in X and Y";
                return false;
            }
        }
    }
    newId = createAt(config, at.add(unit), {}, feederId, self->enabled());
    cloneSettings(config, newId, feederId, { true, true, true, true, true });
    return true;
}

bool JPPushPullTemplates::setOcrDetectedPart(JPConfiguration& config, const std::string& feederId, const std::string& partId, bool clone,
                                             std::string& why) {
    JPFeeder* f = config.feeder(feederId);
    if (!f) return false;
    if (own(*f).usedAsTemplate) {
        if (!compatibleParts(config, partId, f->partId())) {
            why = "Feeder " + f->name() + " is used as a template and can only be OCR-assigned parts with same tape specification or package.";
            return false;
        }
        f->setPartId(partId);
        return true;
    }
    f->setPartId(partId);
    return !clone || smartClone(config, feederId, partId, { true, true, true, true, true }, why);
}

bool JPPushPullTemplates::identifyPart(const JPConfiguration& config, const std::string& ocrText, double avgScore,
                                       const std::string& feederName, std::string& partId, std::string& why) {
    std::string text = ocrText;
    // A forced line break ("\\" at a line's end) undone; then only the first line.
    for (size_t at; (at = text.find("\\\n")) != std::string::npos;) text.erase(at, 2);
    if (const size_t nl = text.find('\n'); nl != std::string::npos) text = text.substr(0, nl);
    if (const size_t sp = text.find(' '); sp != std::string::npos) {
        bool spaces = false;
        for (const auto& p : config.parts()) spaces = spaces || p->shownName().find(' ') != std::string::npos;
        if (!spaces) text = text.substr(0, sp);
    }
    // OpenPnP's match against a part's id, which is the part's Name here: that name, else one ending "-text"
    // (only one).
    partId.clear();
    std::string matched;
    for (const auto& p : config.parts()) {
        const std::string& name = p->shownName();
        if (name == text) {
            partId = p->id;
            break;
        }
        const std::string tail = "-" + text;
        if (name.size() >= tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0) {
            if (!partId.empty()) {
                why = "OCR part id " + text + " on feeder " + feederName + " matches multiple parts " + matched + " and " + name;
                return false;
            }
            partId = p->id;
            matched = name;
        }
    }
    if (partId.empty()) {
        char score[32];
        std::snprintf(score, sizeof score, "%g", avgScore);
        why = "OCR could not identify/find part id on feeder " + feederName + ", OCR detected part id " + text + " (avg. score=" + score + ")";
        return false;
    }
    return true;
}

std::string JPPushPullTemplates::partsAlphabet(const JPConfiguration& config, const std::string& stock) {
    std::set<char> chars(stock.begin(), stock.end());
    for (const auto& p : config.parts()) chars.insert(p->id.begin(), p->id.end());
    std::string out;
    for (char c : chars)
        if (c != ' ') out += c;
    return out;
}

} // inline namespace jf
