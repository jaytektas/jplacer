// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBlindsFeeder.h"

#include "JPBlindsVision.h"
#include "JPFeederPipelines.h"
#include "JPTravel.h"

#include "common/JPlacerLog.h"
#include "model/JPBlindsFeeders.h"
#include "model/JPPushPullTemplates.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// How long what vision found is shown: looking for a fiducial, otherwise, and a preview.
constexpr int kFiducialShownMs = 250, kShownMs = 1000, kPreviewShownMs = 2000;
// OpenPnP's defaults.
constexpr int    kFidLocMaxPasses = 3, kCoverCalibrationPasses = 3;
constexpr double kFidLocToleranceMm = 0.5, kPocketPosToleranceMm = 0.1, kEdgeDistanceMm = 2, kPushZOffsetMm = 0.25, kPushSpeed = 0.1,
                 kSprocketPitchMm = 4;

double mm(const JPLength& l) { return l.convertToUnits(kMm).value(); }
double length(const JPFeeder& f, const char* e, double def) { return mm(f.lengthOf(e, JPLength(def, kMm))); }

// OpenPnP's NozzleAndTipForPushing: the nozzle that pushes, its tip, and the tip it had when one was loaded for it.
struct Pusher {
    std::string nozzleId, tipId, tipBefore;
    bool        changed = false;
};

// One blinds feeder's work on the machine.
class Blinds {
public:
    Blinds(JPConfiguration& config, std::string id, JPJobMachine& machine, const JPBlindsFeeder::OnMain& onMain)
        : m_config(config), m_id(std::move(id)), m_machine(machine), m_onMain(onMain) {}

    void main(const std::function<void()>& fn) const {
        if (m_onMain) m_onMain(fn);
        else fn();
    }
    // The feeder as it is now (a copy: the list may move).
    std::optional<JPFeeder> feeder() const {
        std::optional<JPFeeder> f;
        main([&] {
            if (const JPFeeder* x = m_config.feeder(m_id)) f = *x;
        });
        return f;
    }
    void edit(const std::function<void(JPConfiguration&, JPFeeder&)>& fn) {
        main([&] {
            if (JPFeeder* f = m_config.feeder(m_id)) fn(m_config, *f);
        });
    }
    bool missing(std::string& why) const {
        why = "no feeder " + m_id;
        return false;
    }

    // OpenPnP's ensureCameraZ: a camera whose scale depends on Z needs the part's Z.
    bool cameraZ(std::string& why) {
        JPJobMachine::Sight sight;
        if (!m_machine.cameraSight(sight, why)) return false;
        const auto f = feeder();
        if (!f) return missing(why);
        if (sight.twoHeights && f->location().z() == 0) {
            why = "Feeder " + f->name() + ": Please set the Part Z first, it is required to determine the true scale of the camera view for "
                                          "accurate computer vision.";
            return false;
        }
        return true;
    }

    // The camera over `at`, the pipeline run (OCR as `ocr` says), the features found and shown for `showMs`.
    bool look(const JPLocation& at, const std::string& ocr, int showMs, JPBlindsVision::Found& found, JPJobMachine::Sight& sight,
              std::string& why) {
        std::optional<JPPipeline> pipeline;
        main([&] {
            const JPFeeder* f = m_config.feeder(m_id);
            if (!f) return;
            pipeline = JPFeederPipelines::of(*f);
            if (!pipeline) return;
            pipeline->context().configurationDirectory = m_config.directory();
            pipeline->context().label = "feeder " + f->id();
            JPFeederPipelines::setupBlindsOcr(m_config, *f, *pipeline, at, ocr);
        });
        if (!pipeline) return missing(why);
        if (!m_machine.lookThrough(at, *pipeline, sight, why)) return false;
        const auto f = feeder();
        if (!f || !JPBlindsVision::find(*f, *pipeline, sight, found, why)) return false;
        cv::Mat shown = pipeline->workingImage().clone();
        if (shown.channels() == 1) cv::cvtColor(shown, shown, cv::COLOR_GRAY2BGR);
        JPBlindsVision::draw(shown, *f, found, sight);
        m_machine.showOnCamera(shown, showMs);
        return true;
    }

    // OpenPnP's locateFiducial: looked at from where it should be, then from where it was found, until it moves little.
    bool locateFiducial(JPLocation location, JPLocation& out, std::string& why) {
        const auto f = feeder();
        if (!f) return missing(why);
        if (!location.isInitialized()) {
            why = "Feeder " + f->name() + ": Fiducial location not set.";
            return false;
        }
        if (!cameraZ(why)) return false;
        const int passes = f->number("fid-loc-max-passes", kFidLocMaxPasses);
        const double tolerance = f->real("fid-loc-tolerance-mm", kFidLocToleranceMm);
        for (int i = 0; i < passes; ++i) {
            JPBlindsVision::Found found;
            JPJobMachine::Sight sight;
            if (!look(location, "None", kFiducialShownMs, found, sight, why)) return false;
            if (found.fiducials.empty()) {
                why = "Feeder " + f->name() + ": Fiducial not found.";
                return false;
            }
            const cv::RotatedRect& best = found.fiducials.front();
            const JPLocation at = sight.toMachine(best.center.x, best.center.y);
            const double distance = at.linearDistanceTo(location);
            JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "bestFiducialLocation: " << at.text() << ", mmDistance " << distance;
            location = at;
            if (distance < tolerance) break;
        }
        out = location;
        return true;
    }

    // OpenPnP's calibrateFeederLocations.
    bool calibrateFiducials(std::string& why) {
        const auto f = feeder();
        if (!f) return missing(why);
        if (f->blinds.calibrating) return true;
        if (!m_machine.isHomed()) {
            why = "Feeder " + f->name() + ": Machine not yet homed.";
            return false;
        }
        edit([](JPConfiguration&, JPFeeder& x) { x.blinds.calibrating = true; });
        const bool ok = [&] {
            JPLocation l(kMm);
            if (!locateFiducial(feeder()->locationOf("fiducial-1-location"), l, why)) return false;
            edit([&](JPConfiguration& c, JPFeeder&) { JPBlindsFeeders::setFiducial(c, m_id, 1, l); });
            if (!locateFiducial(feeder()->locationOf("fiducial-3-location"), l, why)) return false;
            edit([&](JPConfiguration& c, JPFeeder&) { JPBlindsFeeders::setFiducial(c, m_id, 3, l); });
            const JPLocation two = feeder()->locationOf("fiducial-2-location");
            if (m_machine.cameraReaches(two)) {
                if (!locateFiducial(two, l, why)) return false;
                edit([&](JPConfiguration& c, JPFeeder&) { JPBlindsFeeders::setFiducial(c, m_id, 2, l); });
            } else {
                // Out of the camera's reach: rebuilt square from fiducials 1 and 3, a tape length along.
                edit([&](JPConfiguration& c, JPFeeder& x) {
                    const JPLocation f1 = x.locationOf("fiducial-1-location").convertToUnits(kMm);
                    const JPLocation f3 = x.locationOf("fiducial-3-location").convertToUnits(kMm);
                    const double d = f3.linearDistanceTo(f1);
                    const double yx = (f3.x() - f1.x()) / d, yy = (f3.y() - f1.y()) / d;
                    const double scale = length(x, "tape-length", 0);
                    JPBlindsFeeders::setFiducial(c, m_id, 2, f1.add(JPLocation(kMm, yy * scale, -yx * scale, 0, 0)));
                });
            }
            edit([&](JPConfiguration& c, JPFeeder&) { JPBlindsFeeders::setCalibrated(c, m_id, true); });
            return true;
        }();
        edit([](JPConfiguration&, JPFeeder& x) { x.blinds.calibrating = false; });
        return ok;
    }

    // OpenPnP's assertCalibration.
    bool assertCalibration(std::string& why) {
        const auto f = feeder();
        if (!f) return missing(why);
        if (f->flag("vision-enabled", true) && !f->blinds.calibrated && !calibrateFiducials(why)) return false;
        edit([](JPConfiguration&, JPFeeder& x) { JPBlindsFeeders::recalculateGeometry(x); });
        return true;
    }

    // OpenPnP's findCoverPosition: from where the camera is sent, where the cover's blinds are.
    bool findCoverPosition(const JPLocation& at, std::string& why) {
        JPBlindsVision::Found found;
        JPJobMachine::Sight sight;
        if (!look(at, "None", kShownMs, found, sight, why)) return false;
        if (std::isnan(found.pocketPositionMm)) {
            why = "Feeder " + feeder()->name() + ": Pocket position not found.";
            return false;
        }
        edit([&](JPConfiguration& c, JPFeeder&) { JPBlindsFeeders::setCoverPosition(c, m_id, found.pocketPositionMm); });
        return true;
    }

    // OpenPnP's isCoverOpenChecked: seen from the second pocket when not known.
    bool coverOpenChecked(bool& open, std::string& why) {
        if (!assertCalibration(why)) return false;
        const auto f = feeder();
        if (!f->blinds.coverPositionMm && !findCoverPosition(JPBlindsFeeders::pickLocation(*f, 2), why)) return false;
        open = JPBlindsFeeders::coverState(*feeder(), true);
        return true;
    }

    // OpenPnP's getNozzleAndTipForPushing: `preferred` when free with a tip
    // that pushes; else any such; else (when `load`) a free nozzle given a
    // pushing tip no other nozzle holds.
    bool pusher(const std::string& preferred, bool load, Pusher& p, std::string& why) {
        const std::vector<JPJobMachine::Nozzle> nozzles = m_machine.nozzles();
        auto pushes = [&](const std::string& tip) { return !tip.empty() && m_machine.tipPush(tip).allowed; };
        auto free = [&](const JPJobMachine::Nozzle& n) { return m_machine.holdingPart(n.id).empty(); };
        for (const auto& n : nozzles)
            if (n.id == preferred && free(n) && pushes(n.tipId)) {
                p = { n.id, n.tipId, n.tipId, false };
                return true;
            }
        for (const auto& n : nozzles)
            if (free(n) && pushes(n.tipId)) {
                p = { n.id, n.tipId, n.tipId, false };
                return true;
            }
        if (!load) {
            p = {};
            return true;
        }
        for (const auto& n : nozzles) {
            if (!free(n)) continue;
            for (const std::string& tip : n.tipIds) {
                if (!pushes(tip)) continue;
                // Loaded on another nozzle: it holds a part (else it was taken above).
                if (std::any_of(nozzles.begin(), nozzles.end(), [&](const JPJobMachine::Nozzle& o) { return o.tipId == tip; })) continue;
                if (!m_machine.changeTip(n.id, tip, why)) return false;
                p = { n.id, tip, n.tipId, true };
                return true;
            }
        }
        why = "BlindsFeeder: No Nozzle/NozzleTip found that allows pushing and has no part loaded.";
        return false;
    }
    bool restore(const Pusher& p, std::string& why) {
        if (!p.changed || p.tipBefore.empty() || p.tipBefore == p.tipId) return true;
        return m_machine.changeTip(p.nozzleId, p.tipBefore, why);
    }

    // OpenPnP's actuateCover.
    bool actuateCover(const std::string& preferred, bool open, bool load, bool restoreTip, std::string& why) {
        const auto f = feeder();
        if (!f) return missing(why);
        const std::string type = f->text("cover-type", "BlindsCover");
        if (type == "NoCover") {
            why = "Feeder " + f->name() + ": has no cover to actuate.";
            return false;
        }
        if (f->location().z() == 0.0) {
            why = "Feeder " + f->name() + " Part Z not set.";
            return false;
        }
        Pusher p;
        if (!pusher(preferred, load, p, why)) return false;
        if (p.tipId.empty()) {
            why = "Feeder " + f->name() + ": loaded nozzle tips do not allow pushing. Check the nozzle tip configuration or change the nozzle tip.";
            return false;
        }
        const double tip = m_machine.tipPush(p.tipId).diameterLowMm;
        if (tip == 0.) {
            why = "Feeder " + f->name() + ": current nozzle tip " + p.tipId + " has push diameter not set. Check the nozzle tip configuration.";
            return false;
        }
        if (!assertCalibration(why)) return false;
        const JPFeeder g = *feeder();
        const double pitch = length(g, "pocket-pitch", 0), sprocket = length(g, "sprocket-pitch", kSprocketPitchMm);
        const double z = g.location().convertToUnits(kMm).z(), rotation = JPBlindsFeeders::pickRotationInTape(g);
        const double speed = g.real("push-speed", kPushSpeed), pushZ = length(g, "push-Z-offset", kPushZOffsetMm);
        auto place = [&](double x, double y, double atZ) {
            const JPLocation m = JPBlindsFeeders::feederToMachine(g, JPLocation(kMm, x, y, atZ, rotation));
            return std::array<std::optional<double>, 4> { m.x(), m.y(), m.z(), m.rotation() };
        };
        if (type == "BlindsCover") {
            // Along the tape from beyond the cover's edge, half a sprocket too far, the tip's edge on the cover's.
            const double open0 = -length(g, "edge-open-distance", kEdgeDistanceMm) - pitch * 0.5 - sprocket * 0.5 - tip * 0.5;
            const double close0 = length(g, "edge-closed-distance", kEdgeDistanceMm) + length(g, "tape-length", 0) + pitch * 0.5 + sprocket * 0.5
                                  + tip * 0.5;
            const double open1 = -length(g, "edge-open-distance", kEdgeDistanceMm) - tip * 0.5;
            const double close1 = length(g, "edge-closed-distance", kEdgeDistanceMm) + length(g, "tape-length", 0) + tip * 0.5;
            const double y = length(g, "pocket-centerline", 0);
            if (!m_machine.moveNozzle(p.nozzleId, place(open ? open0 : close0, y, z + pushZ), 1.0, true, why)
                || !m_machine.moveNozzle(p.nozzleId, place(open ? open1 : close1, y, z + pushZ), speed, false, why) || !m_machine.safeZ(why))
                return false;
            const double distance = JPBlindsFeeders::pocketDistanceMm(g);
            edit([&](JPConfiguration& c, JPFeeder&) { JPBlindsFeeders::setCoverPosition(c, m_id, open ? distance : distance - pitch * 0.5); });
        } else if (type == "PushCover" && open) {
            // The next pocket uncovered: pushed from behind the cover to it, then down onto the part.
            const JPLocation pick = JPBlindsFeeders::pickLocation(g, JPBlindsFeeders::fedPocket(g) + 1);
            const JPLocation pickFeeder = JPBlindsFeeders::machineToFeeder(g, pick);
            const bool fresh = g.number("feed-count", 0) == 0 || !JPBlindsFeeders::coverState(g, true);
            const double x0 = fresh ? -pitch * 0.5 - tip : *g.blinds.coverPositionMm - pitch * 0.5 - tip;
            const double x1 = pickFeeder.x() + pitch * 0.5 - tip * 0.5;
            const double y = pickFeeder.y();
            const auto end = place(x1, y, z - pushZ);
            if (!m_machine.moveNozzle(p.nozzleId, place(x0, y, z - pushZ), 1.0, true, why)
                || !m_machine.moveNozzle(p.nozzleId, end, speed, false, why)
                || !m_machine.moveNozzle(p.nozzleId, { pick.x(), pick.y(), end[2], pick.rotation() }, 1.0, false, why))
                return false;
            edit([&](JPConfiguration& c, JPFeeder&) { JPBlindsFeeders::setCoverPosition(c, m_id, pickFeeder.x()); });
        }
        return !restoreTip || restore(p, why);
    }

    // OpenPnP's showFeatures.
    bool showFeatures(std::string& why) {
        if (!cameraZ(why)) return false;
        const auto at = m_machine.cameraLocation();
        if (!at) {
            why = "no camera on the head";
            return false;
        }
        JPBlindsVision::Found found;
        JPJobMachine::Sight sight;
        return look(*at, "None", kPreviewShownMs, found, sight, why);
    }

    // OpenPnP's triggerOcrAction.
    bool ocrAction(const std::string& text, double score, const std::string& action, std::string& why) {
        bool ok = true;
        edit([&](JPConfiguration& c, JPFeeder& f) {
            std::string part;
            if (!(ok = JPPushPullTemplates::identifyPart(c, text, score, f.name(), part, why))) return;
            const std::string current = f.partId();
            if (current.empty()) {
                JLOGC(JPlacerLog::kJob, JLogLevel::Trace) << "OCR detected part in feeder " << m_id << ", OCR part " << part;
                f.setPartId(part);
            } else if (part != current) {
                JLOGC(JPlacerLog::kJob, JLogLevel::Trace) << "OCR detected wrong part in slot of feeder " << f.name() << ", current part "
                                                          << current << " != OCR part " << part;
                if (action == "ChangePart") {
                    f.setPartId(part);
                    f.blinds.ocrChangedPartId = current;
                } else if (action == "CheckCorrect") {
                    why = "OCR detected wrong part in slot of feeder " + f.name() + ", current part " + current + " != OCR part " + part;
                    ok = false;
                }
            }
        });
        return ok;
    }

    // OpenPnP's performOcr: over the label's middle.
    bool performOcr(const std::string& action, std::string& why) {
        edit([](JPConfiguration&, JPFeeder& x) { JPBlindsFeeders::recalculateGeometry(x); });
        const JPFeeder f = *feeder();
        const auto corners = JPBlindsFeeders::ocrRegionCorners(f, length(f, "pocket-centerline", 0));
        const JPLocation at = JPBlindsFeeders::feederToMachine(f, corners[1].add(corners[2]).multiply(0.5));
        JPBlindsVision::Found found;
        JPJobMachine::Sight sight;
        if (!look(at, action, kShownMs, found, sight, why)) return false;
        if (!found.ocrText) {
            why = "Feeder " + f.name() + " is missing an \"OCR\" stage in the pipeline.";
            return false;
        }
        JLOGC(JPlacerLog::kJob, JLogLevel::Trace) << "OCR text " << *found.ocrText;
        return ocrAction(*found.ocrText, found.ocrAvgScore, action, why);
    }

    // OpenPnP's findPocketsAndCenterline.
    bool autoSetup(std::string& why) {
        const auto at = m_machine.cameraLocation();
        if (!at) {
            why = "no camera on the head";
            return false;
        }
        // Settings from a feeder already on this holder (and its Z, unless set).
        edit([&](JPConfiguration& c, JPFeeder&) {
            const std::string templ = JPBlindsFeeders::adoptFromConnected(c, m_id, *at, false);
            JPFeeder* f = c.feeder(m_id);
            if (!templ.empty() && f->location().z() == 0)
                f->setLocation(f->location().derive(std::nullopt, std::nullopt, c.feeder(templ)->location().convertToUnits(f->location().units()).z(),
                                                    std::nullopt));
        });
        auto f = feeder();
        if (!(f->locationOf("fiducial-1-location").isInitialized() && f->locationOf("fiducial-2-location").isInitialized()
              && f->locationOf("fiducial-3-location").isInitialized())) {
            why = "Feeder " + f->name() + ": Please set the fiducials first (camera center is outside any previously defined fiducial area).";
            return false;
        }
        if (f->text("cover-type", "BlindsCover") != "BlindsCover") {
            // Only the centerline, from the camera, to the whole mm.
            edit([&](JPConfiguration& c, JPFeeder& x) {
                JPBlindsFeeders::setPocketCenterline(c, m_id, std::floor(JPBlindsFeeders::machineToFeeder(x, *at).y() + 0.5));
            });
            return true;
        }
        if (!cameraZ(why)) return false;
        // The specs found freely.
        edit([](JPConfiguration&, JPFeeder& x) {
            for (const char* e : { "pocket-centerline", "pocket-pitch", "pocket-size" }) x.setLengthOf(e, JPLength(0, kMm));
        });
        const std::string action = f->text("ocr-action", "None");
        JPBlindsVision::Found found;
        JPJobMachine::Sight sight;
        if (!look(*at, action, kShownMs, found, sight, why)) return false;
        const std::string name = f->name();
        if (std::isnan(found.pocketCenterlineMm)) why = "Feeder " + name + ": Tape centerline not found.";
        else if (std::isnan(found.pocketPitchMm)) why = "Feeder " + name + ": Pocket pitch not found.";
        else if (std::isnan(found.pocketSizeMm)) why = "Feeder " + name + ": Pocket size not found.";
        if (!why.empty()) return false;
        edit([&](JPConfiguration& c, JPFeeder&) {
            JPBlindsFeeders::setPocketCenterline(c, m_id, found.pocketCenterlineMm);
            JPFeeder* g = c.feeder(m_id);
            g->setLengthOf("pocket-pitch", JPLength(found.pocketPitchMm, kMm));
            g->setLengthOf("pocket-size", JPLength(found.pocketSizeMm, kMm));
            JPBlindsFeeders::recalculateGeometry(*g);
        });
        if (action != "None") {
            if (!found.ocrText) {
                why = "Feeder " + name + " is missing an \"OCR\" stage in the pipeline.";
                return false;
            }
            JLOGC(JPlacerLog::kJob, JLogLevel::Trace) << "OCR text " << *found.ocrText;
            return ocrAction(*found.ocrText, found.ocrAvgScore, action, why);
        }
        return true;
    }

    // OpenPnP's calibrateCoverEdges.
    bool calibrateCoverEdges(std::string& why) {
        auto f = feeder();
        if (!f) return missing(why);
        if (f->text("cover-type", "BlindsCover") != "BlindsCover") {
            why = "Feeder " + f->name() + ": Only Blinds Cover can be calibrated.";
            return false;
        }
        if (!assertCalibration(why)) return false;
        f = feeder();
        const double pitch = length(*f, "pocket-pitch", 0), half = pitch * 0.5;
        const double wantedOpen = JPBlindsFeeders::pocketDistanceMm(*f), wantedClosed = std::fmod(wantedOpen + half, pitch);
        const double cameraPocket = std::floor((f->number("first-pocket", 1) + f->number("last-pocket", 0)) / 2.0);
        const JPLocation openAt = JPBlindsFeeders::pickLocation(*f, cameraPocket), closedAt = JPBlindsFeeders::pickLocation(*f, cameraPocket - 0.5);
        const double tolerance = f->real("pocket-pos-tolerance-mm", kPocketPosToleranceMm);
        auto wrap = [&](double offset) {
            if (offset > half) offset -= pitch;
            if (offset < -half) offset += pitch;
            return offset;
        };
        // Closed first, a known opposite to start from.
        if (!actuateCover("", false, true, false, why)) return false;
        for (int i = 0; i < kCoverCalibrationPasses; ++i) {
            if (!actuateCover("", true, true, false, why) || !findCoverPosition(openAt, why)) return false;
            const double offsetOpen = wrap(*feeder()->blinds.coverPositionMm - wantedOpen);
            edit([&](JPConfiguration& c, JPFeeder& x) {
                x.setLengthOf("edge-open-distance", JPLength(length(x, "edge-open-distance", kEdgeDistanceMm) + offsetOpen, kMm));
                JPBlindsFeeders::propagate(c, m_id);
            });
            if (!actuateCover("", false, true, false, why) || !findCoverPosition(closedAt, why)) return false;
            const double offsetClosed = wrap(*feeder()->blinds.coverPositionMm - wantedClosed);
            edit([&](JPConfiguration& c, JPFeeder& x) {
                x.setLengthOf("edge-closed-distance", JPLength(length(x, "edge-closed-distance", kEdgeDistanceMm) - offsetClosed, kMm));
                JPBlindsFeeders::propagate(c, m_id);
            });
            if (std::abs(offsetOpen) < tolerance * 0.5 && std::abs(offsetClosed) < tolerance * 0.5) break;
        }
        return true;
    }

    // OpenPnP's feed.
    bool feed(const std::string& nozzleId, std::string& why) {
        auto f = feeder();
        if (!f) return missing(why);
        if (f->number("first-pocket", 1) + f->number("feed-count", 0) > f->number("last-pocket", 0)) {
            why = "Feeder " + f->name() + " part " + f->partId() + " empty.";
            return false;
        }
        if (!assertCalibration(why)) return false;
        f = feeder();
        const std::string type = f->text("cover-type", "BlindsCover"), actuation = f->text("cover-actuation", "OpenOnJobStart");
        if (type == "BlindsCover") {
            if (actuation == "CheckOpen") {
                bool open = false;
                if (!coverOpenChecked(open, why)) return false;
                if (!open) {
                    edit([](JPConfiguration&, JPFeeder& x) { x.blinds.coverPositionMm.reset(); });
                    why = "Feeder " + f->name() + " " + f->partId() + ": cover is not open. Please open manually.";
                    return false;
                }
            } else if ((actuation == "OpenOnFirstUse" || actuation == "OpenOnJobStart") && !JPBlindsFeeders::coverState(*f, true)) {
                if (!actuateCover(nozzleId, true, true, true, why)) return false;
            }
        } else if (type == "PushCover") {
            if (!actuateCover(nozzleId, true, true, true, why)) return false;
        }
        edit([](JPConfiguration&, JPFeeder& x) { x.setNumber("feed-count", x.number("feed-count", 0) + 1); });
        return true;
    }

    // OpenPnP's prepareForJob.
    bool prepareForJob(bool visit, std::string& why) {
        const auto f = feeder();
        if (!f) return missing(why);
        if (visit) {
            if (!assertCalibration(why)) return false;
            edit([](JPConfiguration&, JPFeeder& x) { x.blinds.ocrChangedPartId.clear(); });
            if (f->text("cover-actuation", "OpenOnJobStart") == "OpenOnJobStart" && !JPBlindsFeeders::coverState(*feeder(), true)) {
                // The pusher kept, to put its tip back after all are visited.
                Pusher p;
                if (!pusher("", true, p, why)) return false;
                edit([&](JPConfiguration&, JPFeeder& x) {
                    x.blinds.pushNozzleId = p.changed ? p.nozzleId : std::string();
                    x.blinds.pushTipBefore = p.changed ? p.tipBefore : std::string();
                });
                if (!actuateCover("", true, true, false, why)) return false;
            }
            const std::string action = f->text("ocr-action", "None");
            return action == "None" || performOcr(action, why);
        }
        // Visited: a tip loaded to push put back.
        if (!f->blinds.pushNozzleId.empty()) {
            const Pusher p { f->blinds.pushNozzleId, {}, f->blinds.pushTipBefore, true };
            edit([](JPConfiguration&, JPFeeder& x) { x.blinds.pushNozzleId.clear(); });
            if (!m_machine.changeTip(p.nozzleId, p.tipBefore, why)) return false;
        }
        // A part changed by OCR: the job's checks no longer hold, it stops.
        if (!f->blinds.ocrChangedPartId.empty()) {
            std::string msg = "OCR changed parts: ";
            main([&] {
                for (JPFeeder& x : m_config.feeders())
                    if (x.typeName() == "BlindsFeeder" && !x.blinds.ocrChangedPartId.empty()) {
                        msg += "BlindsFeeder " + x.name() + " part " + f->blinds.ocrChangedPartId + " changed to " + x.partId() + ". ";
                        x.blinds.ocrChangedPartId.clear();
                    }
            });
            why = msg + "Please review.";
            return false;
        }
        return true;
    }

private:
    JPConfiguration&              m_config;
    std::string                   m_id;
    JPJobMachine&                 m_machine;
    const JPBlindsFeeder::OnMain& m_onMain;
};

} // namespace

bool JPBlindsFeeder::feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, JPJobMachine& machine,
                          const OnMain& onMain, std::string& why) {
    return Blinds(config, feederId, machine, onMain).feed(nozzleId, why);
}

bool JPBlindsFeeder::calibrateFiducials(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                                        std::string& why) {
    return Blinds(config, feederId, machine, onMain).calibrateFiducials(why);
}

bool JPBlindsFeeder::showFeatures(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                                  std::string& why) {
    return Blinds(config, feederId, machine, onMain).showFeatures(why);
}

bool JPBlindsFeeder::autoSetup(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                               std::string& why) {
    return Blinds(config, feederId, machine, onMain).autoSetup(why);
}

bool JPBlindsFeeder::performOcr(JPConfiguration& config, const std::string& feederId, const std::string& action, JPJobMachine& machine,
                                const OnMain& onMain, std::string& why) {
    return Blinds(config, feederId, machine, onMain).performOcr(action, why);
}

bool JPBlindsFeeder::calibrateCoverEdges(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain,
                                         std::string& why) {
    return Blinds(config, feederId, machine, onMain).calibrateCoverEdges(why);
}

bool JPBlindsFeeder::actuateCover(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, bool open,
                                  JPJobMachine& machine, const OnMain& onMain, std::string& why) {
    return Blinds(config, feederId, machine, onMain).actuateCover(nozzleId, open, true, false, why);
}

bool JPBlindsFeeder::actuateAllCovers(JPConfiguration& config, const std::string& nozzleId, bool open, JPJobMachine& machine,
                                      const OnMain& onMain, std::string& why) {
    std::vector<std::string> ids;
    std::vector<JPLocation> places;
    auto main = [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    main([&] {
        ids = JPBlindsFeeders::coversToActuate(config, { "Manual", "CheckOpen", "OpenOnFirstUse", "OpenOnJobStart" }, open);
        // Each at the end of its cover it is pushed from.
        for (const std::string& id : ids) {
            const JPFeeder* f = config.feeder(id);
            places.push_back(JPBlindsFeeders::pickLocation(*f, open ? 0 : f->number("pocket-count", 0) + 1));
        }
    });
    if (ids.empty()) {
        why = std::string("No feeders found to ") + (open ? "open." : "close.");
        return false;
    }
    // The pusher first (it may need a tip); the way from where the camera is.
    Pusher p;
    if (!Blinds(config, ids.front(), machine, onMain).pusher(nozzleId, true, p, why)) return false;
    for (const size_t i : JPTravel::order(places, machine.cameraLocation(), std::nullopt, machine.travelCost()))
        if (!Blinds(config, ids[i], machine, onMain).actuateCover("", open, true, false, why)) return false;
    return true;
}

bool JPBlindsFeeder::prepareForJob(JPConfiguration& config, const std::string& feederId, bool visit, JPJobMachine& machine,
                                   const OnMain& onMain, std::string& why) {
    return Blinds(config, feederId, machine, onMain).prepareForJob(visit, why);
}

} // inline namespace jf
