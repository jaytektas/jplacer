// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFiducialLocator.h"

#include "JPVisionPipelinePrep.h"

#include "setup/JPVisionPipelines.h"

#include "common/JPlacerLog.h"
#include "model/JPFiducialFit.h"
#include "model/JPPanel.h"
#include "model/JPPanelLocation.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

inline namespace jf {

namespace {

// The fiducial OpenPnP stands in when there is no part or package (VisionUtils.readyHomingFiducialWithDiameter).
constexpr double kStandInFiducialMm = 1.0;


JPLocation mm(const JPLocation& l) { return l.convertToUnits(JPLengthUnit::Millimeters); }

double distance(const JPLocation& a, const JPLocation& b) {
    const JPLocation x = mm(a), y = mm(b);
    return std::hypot(x.x() - y.x(), x.y() - y.y());
}

std::string format(const char* fmt, double a, double b, double c) {
    char buf[200];
    std::snprintf(buf, sizeof buf, fmt, a, b, c);
    return buf;
}

} // namespace

JPFiducialLocator::PartProblem JPFiducialLocator::partLook(JPConfiguration& config, const JPPart& part,
                                                           const JPVisionConfig& vision, double& diameterMm,
                                                           JPJobMachine::FiducialLook& look, std::string& settingsName) {
    // Its size: the fiducial's pad, as its package's footprint draws it.
    const JPPackage* package = config.package(part.packageId);
    diameterMm = 0;
    if (package && !package->footprint.pads.empty()) {
        const JPFootprint::Pad& pad = package->footprint.pads.front();
        diameterMm = JPLength(std::max(pad.width, pad.height), package->footprint.units).convertToUnits(JPLengthUnit::Millimeters).value();
    }
    if (diameterMm <= 0) return PartProblem::NoSize;
    // Looked at as its fiducial vision settings say (the part's, its package's, the machine's).
    look = {};
    look.partId = part.id;   // whose the look is, named in what is said of it
    look.averaging = vision.enabledAveraging;
    if (const JPVisionSettings* v = config.inheritedVision(part, JPVisionSettings::Kind::Fiducial, vision.fiducialVisionId)) {
        settingsName = v->name;
        if (!v->enabled) return PartProblem::Disabled;
        look.passes = v->number("max-vision-passes", 3);
        look.maxLinearOffsetMm = v->lengthMm("max-linear-offset", 0.2);
        look.parallaxDiameterMm = v->lengthMm("parallax-diameter", 0);
        look.parallaxAngle = v->real("parallax-angle", 0);
        // By its OpenPnP pipeline, prepared for its part (the camera given it where it is used).
        if (vision.fiducialPipeline) {
            look.pipeline = std::make_shared<JPPipeline>(JPVisionPipelines::of(*v));
            look.pipeline->context().configurationDirectory = config.directory();
            look.pipeline->context().label = "fiducial " + part.id;
            JPVisionPipelinePrep::fiducial(*look.pipeline, config, *v, part.id, part.packageId, 0, vision.fiducialMaxDistanceMm);
        }
    }
    return PartProblem::None;
}

JPFiducialLocator::PartProblem JPFiducialLocator::lookFor(JPConfiguration& config, const std::string& partId,
                                                          const std::string& packageId, const JPVisionConfig& vision,
                                                          double& diameterMm, JPJobMachine::FiducialLook& look,
                                                          std::string& settingsName) {
    if (const JPPart* part = config.part(partId)) return partLook(config, *part, vision, diameterMm, look, settingsName);
    // A package, or neither: a part standing in for it.
    JPPart stand;
    stand.packageId = packageId;
    const PartProblem p = partLook(config, stand, vision, diameterMm, look, settingsName);
    if (p != PartProblem::NoSize || !packageId.empty()) return p;
    // Neither: as OpenPnP's FIDUCIAL-HOME stands in, a round 1 mm fiducial,
    // looked at as the machine's fiducial vision settings say.
    diameterMm = kStandInFiducialMm;
    look = {};
    look.averaging = vision.enabledAveraging;
    if (const JPVisionSettings* v = config.visionSettings(vision.fiducialVisionId)) {
        settingsName = v->name;
        if (!v->enabled) return PartProblem::Disabled;
        look.passes = v->number("max-vision-passes", 3);
        look.maxLinearOffsetMm = v->lengthMm("max-linear-offset", 0.2);
        look.parallaxDiameterMm = v->lengthMm("parallax-diameter", 0);
        look.parallaxAngle = v->real("parallax-angle", 0);
        if (vision.fiducialPipeline) {
            look.pipeline = std::make_shared<JPPipeline>(JPVisionPipelines::of(*v));
            look.pipeline->context().configurationDirectory = config.directory();
            look.pipeline->context().label = "fiducial";
            JPVisionPipelinePrep::fiducial(*look.pipeline, config, *v, "", "", 0, vision.fiducialMaxDistanceMm);
        }
    }
    return PartProblem::None;
}

JPFiducialLocator::Result JPFiducialLocator::locate(JPConfiguration& config, JPJobMachine& machine, const OnMain& onMain,
                                                    const std::vector<JPPlacementsHolderLocation*>& locations,
                                                    const Tolerances& tolerances) {
    machine.startFiducialCheck();   // one check: the head camera tuned once, on its first fiducial
    Result r;
    auto main = [&](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    struct Fiducial {
        JPPlacementsHolderLocation* location;
        JPPlacement                 placement;
        JPLocation                  nominal { JPLengthUnit::Millimeters };
        double                      diameterMm = 0;
        JPLocation                  measured { JPLengthUnit::Millimeters };
        JPJobMachine::FiducialLook  look;
    };
    std::vector<Fiducial> fiducials;
    bool ok = true;
    main([&] {
        for (JPPlacementsHolderLocation* lp : locations) {
            JPPlacementsHolderLocation& l = *lp;
            std::vector<JPPlacement> placements = l.holder ? l.holder->placements : std::vector<JPPlacement> {};
            if (l.kind() == JPPlacementsHolderLocation::Kind::Panel)
                if (const JPPanel* panel = static_cast<JPPanelLocation&>(l).panel())
                    for (const JPPlacement& p : panel->pseudoPlacements()) placements.push_back(p);
            std::vector<JPPlacement> fids;
            for (const JPPlacement& p : placements)
                if (p.type == JPPlacement::Type::Fiducial && p.side == l.globalSide() && p.enabled) fids.push_back(p);
            if (fids.size() < 2) {
                r.id = l.uniqueId();
                r.message = "The panel/board side contains only " + std::to_string(fids.size()) +
                            " placements marked as fiducials, but at least 2 are required.";
                ok = false;
                return;
            }
            // Measured where they are, not where the last check put them.
            l.setLocalToParentTransform(std::nullopt);
            for (const JPPlacement& p : fids) {
                const JPPart* part = config.part(p.partId);
                if (!part) {
                    r.about = Result::About::Placement;
                    r.id = l.uniqueId();
                    r.placementId = p.id;
                    r.message = "Fiducial " + p.id + " does not have a valid part assigned.";
                    ok = false;
                    return;
                }
                double diameter = 0;
                JPJobMachine::FiducialLook look;
                std::string settings;
                const PartProblem problem = partLook(config, *part, tolerances.vision, diameter, look, settings);
                if (problem != PartProblem::None) {
                    r.about = Result::About::Part;
                    r.id = part->id;
                    r.message = problem == PartProblem::NoSize
                                    ? "Fiducial " + p.id + "'s part " + part->id + " has no footprint pad to give its size."
                                    : "Part " + part->id + " fiducial vision settings " + settings + " are disabled.";
                    ok = false;
                    return;
                }
                fiducials.push_back({ &l, p, l.placementLocation(p.location), diameter, JPLocation(JPLengthUnit::Millimeters), look });
            }
        }
    });
    if (!ok) return r;

    // Visited nearest first from where the camera is.
    std::vector<bool> done(fiducials.size(), false);
    std::optional<JPLocation> at = machine.cameraLocation();
    for (size_t k = 0; k < fiducials.size(); ++k) {
        size_t best = 0;
        double bestD = std::numeric_limits<double>::max();
        for (size_t i = 0; i < fiducials.size(); ++i) {
            if (done[i]) continue;
            const double d = at ? distance(*at, fiducials[i].nominal) : double(i);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        done[best] = true;
        Fiducial& f = fiducials[best];
        std::string why;
        if (!machine.locateFiducial(f.nominal, f.diameterMm, f.look, f.measured, why)) {
            r.id = f.location->uniqueId();
            r.message = "Unable to locate " + f.placement.id + " on " + f.location->uniqueId() + ": " + why;
            return r;
        }
        at = f.measured;
        JLOGC(JPlacerLog::kJob, JLogLevel::Info) << "found " << f.placement.id << " on " << f.location->uniqueId() << " at "
                                                 << f.measured.x() << ", " << f.measured.y();
    }

    // Each set where its fiducials say, within the tolerances.
    main([&] {
        for (JPPlacementsHolderLocation* lp : locations) {
            JPPlacementsHolderLocation& l = *lp;
            const bool bottom = l.globalSide() == JPSide::Bottom;
            const JPLocation saved = l.globalLocation();
            std::vector<JPLocation> expected, measured;
            for (const Fiducial& f : fiducials)
                if (f.location == &l) {
                    expected.push_back(f.placement.location.invert(bottom, false, false, false));
                    measured.push_back(f.measured);
                }
            JPAffineTransform tx = JPFiducialFit::derive(expected, measured);
            if (bottom) tx.scale(-1, 1);
            l.setLocalToGlobalTransform(tx);
            JPLocation origin(JPLengthUnit::Millimeters);
            if (bottom && l.holder) origin = mm(l.holder->dimensions).derive(std::nullopt, 0.0, 0.0, 0.0);
            const JPLocation moved = l.placementLocation(origin);
            const double offset = distance(moved, saved);
            const JPAffineTransform::Info ai = tx.info();
            JLOGC(JPlacerLog::kJob, JLogLevel::Info)
                << l.uniqueId() << " fiducial results: scale " << ai.xScale << ", " << ai.yScale << ", shear " << ai.xShear
                << ", rotation " << ai.rotationAngleDeg << ", origin offset " << offset << " mm";
            const double st = tolerances.scaling, sh = tolerances.shearing;
            std::string err;
            if (ai.xScale > 0 && std::abs(ai.xScale - 1) > st)
                err += format("x scaling = %.5f which is outside the expected range of [%.5f, %.5f], ", ai.xScale, 1 - st, 1 + st);
            else if (ai.xScale < 0 && std::abs(ai.xScale + 1) > st)
                err += format("x scaling = %.5f which is outside the expected range of [-%.5f, -%.5f], ", ai.xScale, 1 + st,
                              1 - st);
            if (std::abs(ai.yScale - 1) > st)
                err += format("the y scaling = %.5f which is outside the expected range of [%.5f, %.5f], ", ai.yScale,
                              1 - st, 1 + st);
            if (std::abs(ai.xShear) > sh)
                err += format("the x shearing = %.5f which is outside the expected range of [%.5f, %.5f], ", ai.xShear, -sh, sh);
            if (offset > tolerances.boardLocationMm)
                err += format("the board origin moved %.4fmm which is greater than the allowed amount of %.4fmm, ", offset,
                              tolerances.boardLocationMm, 0);
            if (!err.empty()) {
                err.resize(err.size() - 2);
                l.setLocalToParentTransform(std::nullopt);
                r.id = l.uniqueId();
                r.message = "Fiducial locator results are invalid for " + l.uniqueId() + " because: " + err +
                            ". Potential remidies include setting the initial board X, Y, Z, and Rotation in the Boards "
                            "panel; using a different set of fiducials; or changing the allowable tolerances.";
                ok = false;
                return;
            }
            r.location = moved.convertToUnits(l.location().units()).derive(std::nullopt, std::nullopt, l.location().z(), std::nullopt);
        }
    });
    r.ok = ok;
    return r;
}

} // inline namespace jf
