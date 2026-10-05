// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPushPullTemplates.h"

#include "JPFeederTape.h"
#include "JPLocationXml.h"

#include <algorithm>
#include <array>
#include <cmath>
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

// OpenPnP's setFeederLocation: `templ`'s places (holes and pick location, push-pull places, OCR region) moved from its tape to `to`.
void setFeederLocation(JPFeeder& f, const JPLocation& to, bool primary, bool pushPull, bool vision, const JPFeeder& templ) {
    const JPLocation from = transformOf(templ);
    if (primary) {
        f.setLocationOf("hole-1-location", relocatedXy(templ.locationOf("hole-1-location"), from, to));
        f.setLocationOf("hole-2-location", relocatedXy(templ.locationOf("hole-2-location"), from, to));
    }
    if (pushPull) {
        // Turned: the lever's X and Y swap.
        const bool rotated = std::abs(std::sin((from.rotation() - to.rotation()) * M_PI / 180)) > std::sin(kRotatedDegrees * M_PI / 180);
        const std::string x = templ.text("calibrate-motion-x", "true"), y = templ.text("calibrate-motion-y", "true");
        f.setText("calibrate-motion-x", rotated ? y : x);
        f.setText("calibrate-motion-y", rotated ? x : y);
        f.setFlag("additive-rotation", templ.flag("additive-rotation", true));
        for (const char* e : { "feed-start-location", "feed-mid-1-location", "feed-mid-2-location", "feed-mid-3-location", "feed-end-location" })
            f.setLocationOf(e, relocated(templ.locationOf(e), from, to));
    }
    if (vision)
        if (auto region = JPPushPullTemplates::ocrRegion(templ)) {
            const double by = to.rotation() - from.rotation();
            region->upperLeft = region->upperLeft.rotateXy(by);
            region->upperRight = region->upperRight.rotateXy(by);
            region->lowerLeft = region->lowerLeft.rotateXy(by);
            if (region->offsets) region->offsets = region->offsets->rotateXy(by);
            JPPushPullTemplates::setOcrRegion(f, region);
        }
    if (primary) f.setLocation(relocated(templ.location(), from, to));
}

// An attribute as the template has it (none: its default).
void copy(JPFeeder& f, const JPFeeder& t, const std::string& a) {
    if (const std::string* v = t.toXml().get(a)) f.setText(a, *v);
    else f.removeAttribute(a);
}

JPXmlElement element(const JPXmlNode& n) {
    JPXmlElement e;
    e.name = n.name;
    for (const auto& [k, v] : n.attributes) e.attributes[k] = v;
    return e;
}

} // namespace

bool JPPushPullTemplates::compatibleParts(const JPConfiguration& config, const std::string& partId1, const std::string& partId2) {
    const JPPart* p1 = partId1.empty() ? nullptr : config.part(partId1);
    const JPPart* p2 = partId2.empty() ? nullptr : config.part(partId2);
    if (!p1 || !p2) return false;
    if (p1->packageId == p2->packageId) return true;
    const JPPackage* k1 = config.package(p1->packageId);
    const JPPackage* k2 = config.package(p2->packageId);
    return k1 && k2 && k1->tapeSpecification && !k1->tapeSpecification->empty() && k1->tapeSpecification == k2->tapeSpecification;
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
    const double feedPitch = mm(self->lengthOf("feed-pitch", JPLength(4, kMm)));
    const double partPitch = mm(self->lengthOf("part-pitch", JPLength(4, kMm)));
    const double width = tapeWidthMm(*self);
    // Ranked by similarity: each test in turn, then nearness.
    auto rank = [&](const JPFeeder* f) {
        return std::array<int, 7> { compatibleParts(config, compare, f->partId()) ? 0 : 1, f->flag("used-as-template", false) ? 0 : 1,
                                    mm(f->lengthOf("feed-pitch", JPLength(4, kMm))) == feedPitch ? 0 : 1,
                                    tapeWidthMm(*f) == width ? 0 : 1, mm(f->lengthOf("part-pitch", JPLength(4, kMm))) == partPitch ? 0 : 1,
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
    if (self->flag("used-as-template", false)) {
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
    if (partOf(config, *self) && compatibleParts(config, self->partId(), t->partId())) {
        if (const JPPackage* pkg = config.package(partOf(config, *self)->packageId)) {
            if (pkg->tapeSpecification && !pkg->tapeSpecification->empty())
                status += " selected by common tape & reel specification " + *pkg->tapeSpecification;
            else
                status += " selected by common package " + pkg->id;
        }
    } else {
        status += " selected as partial match.\nTape & reel specifications/packages are incompatible, settings must be reviewed.";
    }
    return status;
}

void JPPushPullTemplates::cloneSettings(JPConfiguration& config, const std::string& feederId, const std::string& templateId,
                                        const Clone& what) {
    JPFeeder* f = config.feeder(feederId);
    const JPFeeder* tp = config.feeder(templateId);
    if (!f || !tp) return;
    const JPFeeder t = *tp;   // the list may move as f changes
    if (what.location) {
        // Just the pick location's Z, and the options.
        f->setLocation(f->location().derive(t.location(), false, false, true, false));
        f->setFlag("normalize-pick-location", t.flag("normalize-pick-location", true));
        f->setFlag("snap-to-axis", t.flag("snap-to-axis", true));
    }
    if (what.tape) {
        f->setLengthOf("part-pitch", t.lengthOf("part-pitch", JPLength(4, kMm)));
        f->setReal("rotation-in-feeder", t.real("rotation-in-feeder", 0));
        f->setLengthOf("feed-pitch", t.lengthOf("feed-pitch", JPLength(4, kMm)));
        f->setNumber("feed-multiplier", t.number("feed-multiplier", 1));
        f->setNumber("feed-count", 0);
    }
    if (what.pushPull) {
        for (const char* a : { "actuator-name", "peel-off-actuator-name", "feed-speed-push-2", "feed-speed-push-3", "feed-speed-push-end",
                               "feed-speed-pull-3", "feed-speed-pull-2", "feed-speed-pull-1", "feed-speed-pull-0", "included-push-1",
                               "included-push-2", "included-push-3", "included-push-end", "included-pull-3", "included-pull-2",
                               "included-pull-1", "included-pull-0", "included-multi-0", "included-multi-1", "included-multi-2",
                               "included-multi-3", "included-multi-end", "delay-0", "delay-1", "delay-2", "delay-3", "delay-4" })
            copy(*f, t, a);
        f->setChildText("feed-speed-push-1", t.childText("feed-speed-push-1", "1.0"));
    }
    if (what.vision) {
        for (const char* a : { "calibration-trigger", "ocr-font-name", "ocr-font-size-pt", "ocr-wrong-part-action",
                               "ocr-discover-on-job-start", "ocr-stop-after-wrong-part" })
            copy(*f, t, a);
        f->setLengthOf("precision-wanted", t.lengthOf("precision-wanted", JPLength(0.1, kMm)));
        JPFeederTape::resetCalibrationStatistics(*f);
        f->setNumber("feed-count", 0);
    }
    if (what.pipeline) {
        if (const JPXmlNode* p = t.pipeline()) f->setPipeline(*p);
        f->setText("pipeline-type", t.text("pipeline-type", "ColorKeyed"));
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
        if (self->flag("snap-to-axis", true)) {
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
    if (f->flag("used-as-template", false)) {
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

std::optional<JPPushPullTemplates::OcrRegion> JPPushPullTemplates::ocrRegion(const JPFeeder& feeder) {
    const JPXmlNode* n = feeder.child("ocr-region");
    if (!n) return std::nullopt;
    auto location = [n](const char* name) {
        const JPXmlNode* c = n->child(name);
        return c ? JPLocationXml::from(element(*c)) : JPLocation(kMm);
    };
    OcrRegion r;
    r.upperLeft = location("upper-left-corner");
    r.upperRight = location("upper-right-corner");
    r.lowerLeft = location("lower-left-corner");
    const std::string* rectify = n->get("rectify");
    r.rectify = rectify && *rectify == "true";
    if (n->child("offsets")) r.offsets = location("offsets");
    return r;
}

void JPPushPullTemplates::setOcrRegion(JPFeeder& feeder, const std::optional<OcrRegion>& region) {
    if (!region) {
        feeder.removeChild("ocr-region");
        return;
    }
    JPXmlNode n("ocr-region");
    n.attr("rectify", region->rectify ? "true" : "false");
    n.add(JPLocationXml::to("upper-left-corner", region->upperLeft));
    n.add(JPLocationXml::to("upper-right-corner", region->upperRight));
    n.add(JPLocationXml::to("lower-left-corner", region->lowerLeft));
    if (region->offsets) n.add(JPLocationXml::to("offsets", *region->offsets));
    feeder.setChild(std::move(n));
}

bool JPPushPullTemplates::identifyPart(const JPConfiguration& config, const std::string& ocrText, double avgScore,
                                       const std::string& feederName, std::string& partId, std::string& why) {
    std::string text = ocrText;
    // A forced line break ("\\" at a line's end) undone; then only the first line.
    for (size_t at; (at = text.find("\\\n")) != std::string::npos;) text.erase(at, 2);
    if (const size_t nl = text.find('\n'); nl != std::string::npos) text = text.substr(0, nl);
    if (const size_t sp = text.find(' '); sp != std::string::npos) {
        bool spaces = false;
        for (const auto& p : config.parts()) spaces = spaces || p->id.find(' ') != std::string::npos;
        if (!spaces) text = text.substr(0, sp);
    }
    partId.clear();
    for (const auto& p : config.parts()) {
        if (p->id == text) {
            partId = p->id;
            break;
        }
        const std::string tail = "-" + text;
        if (p->id.size() >= tail.size() && p->id.compare(p->id.size() - tail.size(), tail.size(), tail) == 0) {
            if (!partId.empty()) {
                why = "OCR part id " + text + " on feeder " + feederName + " matches multiple parts " + partId + " and " + p->id;
                return false;
            }
            partId = p->id;
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
