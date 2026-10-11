// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBlindsFeeders.h"

#include <algorithm>
#include <cctype>
#include <cmath>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// A place within this of fiducial 1 is on it; the holder reaches this beyond its tape and extent (mm).
constexpr double kOnFiducialMm = 2, kHolderMarginMm = 1;
// Fiducials closer than this are not two (mm).
constexpr double kLeastFiducialDistanceMm = 1;
// The names that mean "no group": the holder's place makes it.
const char* const kLocationGroups[] = { "Default", "DEFAULT", "LOCATION", "NONE", "" };

// Java's Math.round: halves up (not away from zero).
double javaRound(double v) { return std::floor(v + 0.5); }
// A blinds feeder's own (another kind: a new one's, as OpenPnP's defaults).
const JPBlindsFeeder& own(const JPFeeder& f) {
    static const JPBlindsFeeder none;
    const JPBlindsFeeder* b = f.as<JPBlindsFeeder>();
    return b ? *b : none;
}
JPLocation fid(const JPFeeder& f, int n) { return own(f).fiducial(n).convertToUnits(kMm); }

bool isLocationGroup(const std::string& name) {
    return std::any_of(std::begin(kLocationGroups), std::end(kLocationGroups), [&name](const char* g) { return name == g; });
}

std::vector<JPFeeder*> blinds(JPConfiguration& config) {
    std::vector<JPFeeder*> out;
    for (JPFeeder& f : config.feeders())
        if (f.as<JPBlindsFeeder>()) out.push_back(&f);
    return out;
}

JPLocation unitVector(const JPLocation& a, const JPLocation& b) {
    const double d = a.linearDistanceTo(b);
    return JPLocation(kMm, (b.x() - a.x()) / d, (b.y() - a.y()) / d, 0, 0);
}

// The tape length and extent the fiducials give (OpenPnP's updateFeederToMachineTransform's side effects); false without two.
bool measure(const JPFeeder& f, double& tapeLength, double& extent) {
    const JPLocation f1 = fid(f, 1), f2 = fid(f, 2), f3 = fid(f, 3);
    double distance = f1.linearDistanceTo(f2);
    if (!(f1.isInitialized() && f2.isInitialized() && distance > kLeastFiducialDistanceMm)) return false;
    tapeLength = javaRound(distance / 2) * 2;
    const JPLocation ref = f3.linearDistanceTo(f2) < f3.linearDistanceTo(f1) ? f2 : f1;
    distance = f3.isInitialized() ? ref.linearDistanceTo(f3) : 0;
    extent = javaRound(distance);
    return true;
}

} // namespace

JPBlindsFeeders::Frame JPBlindsFeeders::frame(const JPFeeder& f) {
    Frame t;
    const JPLocation origin = fid(f, 1), f2 = fid(f, 2), f3 = fid(f, 3);
    double distance = origin.linearDistanceTo(f2);
    t.tx = origin.x();
    t.ty = origin.y();
    if (!(origin.isInitialized() && f2.isInitialized() && distance > kLeastFiducialDistanceMm)) return t;
    const bool normalize = own(f).normalize;
    // Not normalized: the distance still a multiple of 2 mm, as the holder's sprockets are.
    if (!normalize) distance = javaRound(distance / 2) * 2;
    const double xx = (f2.x() - origin.x()) / distance, xy = (f2.y() - origin.y()) / distance;
    // Fiducial 3 across from fiducial 1 or 2, whichever is nearer.
    const JPLocation ref = f3.linearDistanceTo(f2) < f3.linearDistanceTo(origin) ? f2 : origin;
    double across = f3.isInitialized() ? ref.linearDistanceTo(f3) : 0;
    double yx = -xy, yy = xx;
    if (!(normalize || across < kLeastFiducialDistanceMm)) {
        across = javaRound(across);
        yx = (f3.x() - ref.x()) / across;
        yy = (f3.y() - ref.y()) / across;
    }
    t.a = xx;
    t.b = xy;
    t.c = yx;
    t.d = yy;
    t.rotation = std::atan2(xy, xx) * 180 / M_PI;
    t.fromFiducials = true;
    return t;
}

JPLocation JPBlindsFeeders::feederToMachine(const JPFeeder& f, const JPLocation& l) {
    const Frame t = frame(f);
    const JPLocation m = l.convertToUnits(kMm);
    return JPLocation(kMm, t.a * m.x() + t.c * m.y() + t.tx, t.b * m.x() + t.d * m.y() + t.ty, m.z(), m.rotation() + t.rotation);
}

JPLocation JPBlindsFeeders::machineToFeeder(const JPFeeder& f, const JPLocation& l) {
    const Frame t = frame(f);
    const JPLocation m = l.convertToUnits(kMm);
    const double det = t.a * t.d - t.b * t.c;
    const double x = m.x() - t.tx, y = m.y() - t.ty;
    if (det == 0) return JPLocation(kMm, x, y, m.z(), m.rotation() - t.rotation);
    return JPLocation(kMm, (t.d * x - t.c * y) / det, (-t.b * x + t.a * y) / det, m.z(), m.rotation() - t.rotation);
}

double JPBlindsFeeders::pixelToFeederAngle(const JPFeeder& f, double pixelAngle) { return -(pixelAngle - frame(f).rotation); }

void JPBlindsFeeders::updateFrame(JPConfiguration& config, const std::string& feederId) {
    JPFeeder* f = config.feeder(feederId);
    JPBlindsFeeder* b = f ? f->as<JPBlindsFeeder>() : nullptr;
    if (!b) return;
    double tape = 0, extent = 0;
    if (!measure(*f, tape, extent)) return;
    const bool changed = std::abs(b->tapeLengthMm - tape) > 1e-9 || std::abs(b->feederExtentMm - extent) > 1e-9;
    b->tapeLengthMm = tape;
    b->feederExtentMm = extent;
    recalculateGeometry(*f);
    if (changed) propagate(config, feederId);
}

bool JPBlindsFeeders::isSmallPitch(const JPFeeder& f) {
    const double pitch = own(f).pocketPitchMm;
    return pitch > 0 && std::lround(own(f).sprocketPitchMm / pitch) == 2;
}

double JPBlindsFeeders::pocketDistanceMm(const JPFeeder& f) {
    const double pitch = own(f).pocketPitchMm, sprocket = own(f).sprocketPitchMm;
    if (pitch <= 0) return NAN;
    const double relative = own(f).coverType == "BlindsCover" ? 0.25 : 0.5;
    const double align = sprocket * javaRound(pitch * relative / sprocket - 0.001);
    return sprocket * (isSmallPitch(f) ? 0.0 : 0.5) + align;
}

void JPBlindsFeeders::recalculateGeometry(JPFeeder& f) {
    JPBlindsFeeder* b = f.as<JPBlindsFeeder>();
    if (!b || b->pocketPitchMm <= 0) return;
    const int old = b->pocketCount;
    const int count = int(std::floor(b->tapeLengthMm / b->pocketPitchMm)) + (isSmallPitch(f) ? 1 : 0);
    b->pocketCount = count;
    // The first and last pocket kept within the new count.
    b->firstPocket = std::max(1, std::min(count, b->firstPocket));
    b->lastPocket = std::min(count, std::max(b->firstPocket, b->lastPocket - old + count));
}

double JPBlindsFeeders::pickRotationInTape(const JPFeeder& f) { return f.location().rotation() + 180.; }

JPLocation JPBlindsFeeders::pickLocation(const JPFeeder& f, double pocket) {
    const double x = own(f).pocketPitchMm * (pocket - 1.0) + pocketDistanceMm(f);
    const double y = own(f).pocketCenterlineMm;
    return feederToMachine(f, JPLocation(kMm, x, y, f.location().convertToUnits(kMm).z(), pickRotationInTape(f)));
}

int JPBlindsFeeders::fedPocket(const JPFeeder& f) { return own(f).feedCount + own(f).firstPocket - 1; }

bool JPBlindsFeeders::coverState(const JPFeeder& f, bool open) {
    const std::string& type = own(f).coverType;
    if (type == "NoCover") return open;
    if (!f.blinds.coverPositionMm) return false;
    if (type == "BlindsCover")
        return (std::abs(*f.blinds.coverPositionMm - pocketDistanceMm(f)) < own(f).pocketPosToleranceMm) == open;
    if (type == "PushCover") return (*f.blinds.coverPositionMm > 0.0) == open;
    return false;
}

bool JPBlindsFeeders::isLocationInFeeder(const JPFeeder& f, const JPLocation& location, bool fiducial1Only) {
    const JPLocation f1 = fid(f, 1);
    if (!f1.isInitialized()) return false;
    if (f1.linearDistanceTo(location) < kOnFiducialMm) return true;
    if (fiducial1Only) return false;
    const JPLocation l = machineToFeeder(f, location);
    return l.x() >= -kHolderMarginMm && l.x() <= own(f).tapeLengthMm + kHolderMarginMm && l.y() >= -kHolderMarginMm
        && l.y() <= own(f).feederExtentMm + kHolderMarginMm;
}

std::string JPBlindsFeeders::groupName(const JPFeeder& f) {
    const std::string& name = own(f).feederGroupName;
    return isLocationGroup(name) ? kDefaultGroup : name;
}

std::vector<std::string> JPBlindsFeeders::groupNames(const JPConfiguration& config) {
    std::vector<std::string> out { kDefaultGroup };
    for (const JPFeeder& f : config.feeders())
        if (f.as<JPBlindsFeeder>() && std::find(out.begin(), out.end(), groupName(f)) == out.end()) out.push_back(groupName(f));
    return out;
}

std::vector<std::string> JPBlindsFeeders::connected(const JPConfiguration& config, const std::string& feederId, const JPLocation& location,
                                                    bool fiducial1Only) {
    const JPFeeder* self = config.feeder(feederId);
    std::vector<const JPFeeder*> list;
    if (!self) return {};
    const std::string& group = own(*self).feederGroupName;
    for (const JPFeeder& f : config.feeders())
        if (f.as<JPBlindsFeeder>() && isLocationInFeeder(f, location, fiducial1Only) && own(f).feederGroupName == group)
            list.push_back(&f);
    std::stable_sort(list.begin(), list.end(),
                     [](const JPFeeder* a, const JPFeeder* b) { return own(*a).pocketCenterlineMm < own(*b).pocketCenterlineMm; });
    std::vector<std::string> out;
    for (const JPFeeder* f : list) out.push_back(f->id());
    return out;
}

void JPBlindsFeeders::updateFrom(JPFeeder& to, const JPFeeder& from) {
    JPBlindsFeeder* t = to.as<JPBlindsFeeder>();
    const JPBlindsFeeder* f = from.as<JPBlindsFeeder>();
    if (!t || !f) return;
    t->fiducial1 = f->fiducial1;
    t->fiducial2 = f->fiducial2;
    t->fiducial3 = f->fiducial3;
    t->tapeLengthMm = f->tapeLengthMm;
    t->feederExtentMm = f->feederExtentMm;
    t->normalize = f->normalize;
    // The tape shared by two feeders: its cover and its calibrated edges.
    if (t->pocketCenterlineMm == f->pocketCenterlineMm) {
        to.blinds.coverPositionMm = from.blinds.coverPositionMm;
        t->edgeOpenDistanceMm = f->edgeOpenDistanceMm;
        t->edgeClosedDistanceMm = f->edgeClosedDistanceMm;
    }
    to.blinds.calibrated = from.blinds.calibrated;
    t->visionEnabled = f->visionEnabled;
    t->feederGroupName = groupName(from);
    t->ocrAction = f->ocrAction;
    t->ocrMarginMm = f->ocrMarginMm;
    t->ocrFontName = f->ocrFontName;
    t->ocrFontSizePt = f->ocrFontSizePt;
    t->ocrTextOrientation = f->ocrTextOrientation;
    if (f->pipeline) t->pipeline = f->pipeline;
}

void JPBlindsFeeders::propagate(JPConfiguration& config, const std::string& feederId, const JPLocation& location, bool fiducial1Only) {
    const JPFeeder* self = config.feeder(feederId);
    if (!self) return;
    const JPFeeder from = *self;
    for (const std::string& id : connected(config, feederId, location, fiducial1Only))
        if (id != feederId)
            if (JPFeeder* f = config.feeder(id)) updateFrom(*f, from);
}

void JPBlindsFeeders::propagate(JPConfiguration& config, const std::string& feederId) {
    if (const JPFeeder* f = config.feeder(feederId)) propagate(config, feederId, fid(*f, 1), true);
}

std::string JPBlindsFeeders::adoptFromConnected(JPConfiguration& config, const std::string& feederId, const JPLocation& location,
                                                bool fiducial1Only) {
    std::string templ;
    for (const std::string& id : connected(config, feederId, location, fiducial1Only))
        if (id != feederId) templ = id;
    JPFeeder* f = config.feeder(feederId);
    if (!f) return {};
    if (!templ.empty()) {
        const JPFeeder from = *config.feeder(templ);
        updateFrom(*f, from);
    }
    // With the holder's frame known, the tape's centerline from the place, to the whole mm.
    if (JPBlindsFeeder* b = f->as<JPBlindsFeeder>(); b && fid(*f, 1).isInitialized())
        b->pocketCenterlineMm = javaRound(machineToFeeder(*f, location).y());
    renumber(config, feederId);
    return templ;
}

void JPBlindsFeeders::renumber(JPConfiguration& config, const std::string& feederId) {
    const JPFeeder* self = config.feeder(feederId);
    if (!self) return;
    const std::vector<std::string> list = connected(config, feederId, fid(*self, 1), true);
    int no = 0;
    for (const std::string& id : list)
        if (JPFeeder* f = config.feeder(id))
            if (JPBlindsFeeder* b = f->as<JPBlindsFeeder>()) {
                b->feedersTotal = int(list.size());
                b->feederNo = ++no;
            }
}

void JPBlindsFeeders::setFiducial(JPConfiguration& config, const std::string& feederId, int n, const JPLocation& location) {
    JPFeeder* f = config.feeder(feederId);
    if (!f || !f->as<JPBlindsFeeder>() || n < 1 || n > 3) return;
    const JPLocation old = fid(*f, n);
    const JPLocation now = location.convertToUnits(kMm);
    if (old == now) return;
    f->as<JPBlindsFeeder>()->fiducial(n) = now;
    if (n == 1) {
        if (!(old.isInitialized() || fid(*f, 2).isInitialized() || fid(*f, 3).isInitialized())) {
            // A first fix: the holder's settings from a feeder already on it.
            adoptFromConnected(config, feederId, now, true);
        } else {
            propagate(config, feederId, old, true);
            // Moved far: the whole holder moved, fiducials 2 and 3 with it (2 not turning 3).
            if (old.isInitialized() && old.linearDistanceTo(now) > kOnFiducialMm) {
                f = config.feeder(feederId);
                const JPLocation by = now.subtract(old);
                if (fid(*f, 2).isInitialized()) {
                    const JPLocation two = fid(*f, 2).add(by);
                    f->as<JPBlindsFeeder>()->fiducial2 = JPLocation(kMm);
                    setFiducial(config, feederId, 2, two);
                }
                f = config.feeder(feederId);
                if (fid(*f, 3).isInitialized()) setFiducial(config, feederId, 3, fid(*f, 3).add(by));
            }
        }
    } else if (n == 2) {
        propagate(config, feederId);
        // Fiducial 2 moved: the holder turned about fiducial 1, fiducial 3 with it.
        const JPLocation f1 = fid(*f, 1), f3 = fid(*f, 3);
        if (old.isInitialized() && old.linearDistanceTo(f1) > kOnFiducialMm && f3.isInitialized() && f1.isInitialized()) {
            const JPLocation x = unitVector(f1, now), y(kMm, -x.y(), x.x(), 0, 0);
            if (f1.linearDistanceTo(f3) < old.linearDistanceTo(f3)) {
                const double extent = f3.linearDistanceTo(f1);
                setFiducial(config, feederId, 3, f1.add(y.multiply(extent, extent, 0, 0)));
            } else {
                const double extent = f3.linearDistanceTo(old);
                setFiducial(config, feederId, 3, now.add(y.multiply(extent, extent, 0, 0)));
            }
        }
    } else {
        propagate(config, feederId);
    }
    updateFrame(config, feederId);
}

void JPBlindsFeeders::setPocketCenterline(JPConfiguration& config, const std::string& feederId, double v) {
    JPFeeder* f = config.feeder(feederId);
    JPBlindsFeeder* b = f ? f->as<JPBlindsFeeder>() : nullptr;
    if (!b) return;
    b->pocketCenterlineMm = v;
    renumber(config, feederId);
}

void JPBlindsFeeders::setGroupName(JPConfiguration& config, const std::string& feederId, const std::string& name) {
    JPFeeder* f = config.feeder(feederId);
    if (!f || !f->as<JPBlindsFeeder>() || own(*f).feederGroupName == name) return;
    const std::string oldName = own(*f).feederGroupName;
    std::string proposed = name;
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    if (isLocationGroup(upper)) proposed = kDefaultGroup;
    const std::vector<std::string> connectedNow = connected(config, feederId, fid(*f, 1), true);
    std::vector<std::string> withNew;
    for (const JPFeeder& o : config.feeders())
        if (o.as<JPBlindsFeeder>() && groupName(o) == proposed) withNew.push_back(o.id());
    const std::vector<std::string> names = groupNames(config);
    const bool proposedIsDefault = proposed == kDefaultGroup;
    const bool exists = std::find(names.begin(), names.end(), proposed) != names.end();
    const bool unplaced = !(fid(*f, 1).isInitialized() && fid(*f, 2).isInitialized() && fid(*f, 3).isInitialized());
    // Joining a named group (not yet placed, or alone on its holder); renaming a placed holder's group; leaving for the default.
    if ((unplaced && !withNew.empty()) || (!unplaced && exists && connectedNow.size() <= 1 && !withNew.empty())) {
        const JPFeeder from = *config.feeder(withNew.front());
        updateFrom(*config.feeder(feederId), from);
    } else if (!unplaced && !proposedIsDefault && !exists) {
        for (const std::string& id : connectedNow) config.feeder(id)->as<JPBlindsFeeder>()->feederGroupName = proposed;
    } else if (!unplaced && std::find(names.begin(), names.end(), oldName) != names.end() && proposedIsDefault) {
        config.feeder(feederId)->as<JPBlindsFeeder>()->feederGroupName = kDefaultGroup;
    }
}

void JPBlindsFeeders::setCoverPosition(JPConfiguration& config, const std::string& feederId, std::optional<double> v) {
    JPFeeder* f = config.feeder(feederId);
    if (!f) return;
    const bool known = f->blinds.coverPositionMm.has_value();
    f->blinds.coverPositionMm = v;
    // Shared, as OpenPnP's, when it becomes known or unknown.
    if (known != v.has_value()) propagate(config, feederId);
}

void JPBlindsFeeders::setCalibrated(JPConfiguration& config, const std::string& feederId, bool calibrated) {
    JPFeeder* f = config.feeder(feederId);
    if (!f || f->blinds.calibrated == calibrated) return;
    f->blinds.calibrated = calibrated;
    propagate(config, feederId);
}

std::array<JPLocation, 3> JPBlindsFeeders::ocrRegionCorners(const JPFeeder& f, double feederYMm) {
    const JPBlindsFeeder& b = own(f);
    const double marginSigned = b.ocrMarginMm, margin = std::abs(marginSigned);
    const double halfLabel = b.maxLabelSizeMm * 0.5, offset = b.tapeCenterOffsetMm;
    const double y0 = feederYMm + offset - halfLabel, y1 = feederYMm + offset + halfLabel;
    // A negative margin: the labels at the holder's far end.
    const double edge = marginSigned < 0 ? b.tapeLengthMm + b.edgeClosedDistanceMm + margin : -b.edgeOpenDistanceMm;
    if (b.ocrTextOrientation == "AwayFromTape")
        return { JPLocation(kMm, edge, y0, 0, 0), JPLocation(kMm, edge - margin, y0, 0, 0), JPLocation(kMm, edge, y1, 0, 0) };
    return { JPLocation(kMm, edge - margin, y1, 0, 0), JPLocation(kMm, edge, y1, 0, 0), JPLocation(kMm, edge - margin, y0, 0, 0) };
}

std::vector<std::string> JPBlindsFeeders::coversToActuate(const JPConfiguration& config, const std::vector<std::string>& actuations, bool open) {
    std::vector<std::string> out;
    for (const JPFeeder& f : config.feeders()) {
        if (!f.as<JPBlindsFeeder>() || own(f).coverType != "BlindsCover") continue;
        if (std::find(actuations.begin(), actuations.end(), own(f).coverActuation) == actuations.end()) continue;
        // Enabled ones; and, to close, those known open.
        if (!(f.enabled() || (coverState(f, true) && !open))) continue;
        if (!coverState(f, open)) out.push_back(f.id());
    }
    return out;
}

std::optional<JPLocation> JPBlindsFeeders::jobPreparationLocation(const JPFeeder& f) {
    const bool calibrate = own(f).visionEnabled && !f.blinds.calibrated;
    const bool ocr = own(f).ocrAction != "None";
    const bool open = own(f).coverActuation == "OpenOnJobStart" && !coverState(f, true);
    if (calibrate || ocr || open) return pickLocation(f, 0);
    return std::nullopt;
}

} // inline namespace jf
