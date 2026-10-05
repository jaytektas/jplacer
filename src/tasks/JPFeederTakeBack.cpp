// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeederTakeBack.h"

#include "JPFeederFeed.h"
#include "JPHeapFeeder.h"

#include "model/JPBlindsFeeders.h"

inline namespace jf {

std::string JPFeederTakeBack::feederFor(const JPConfiguration& config, const std::string& partId,
                                        const std::optional<JPLocation>& cameraAt) {
    // OpenPnP's FeederUtils.findClosest: of the enabled feeders holding the part, the nearest to the camera.
    std::string best;
    double bestDistance = 0;
    for (const JPFeeder& f : config.feeders()) {
        if (!f.enabled() || f.partId() != partId || !f.canTakeBackPart()) continue;
        const std::optional<JPLocation> at = f.pickLocation();
        const double d = at && cameraAt ? at->convertToUnits(JPLengthUnit::Millimeters).linearDistanceTo(
                                              cameraAt->convertToUnits(JPLengthUnit::Millimeters))
                                        : 0;
        if (best.empty() || d < bestDistance) {
            best = f.id();
            bestDistance = d;
        }
    }
    return best;
}

bool JPFeederTakeBack::takeBack(JPConfiguration& config, const std::string& nozzleId, JPJobMachine& machine, const OnMain& onMain,
                                JPScripting* scripting, std::string& why) {
    auto main = [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    std::string nozzleName = nozzleId;
    for (const auto& n : machine.nozzles())
        if (n.id == nozzleId) nozzleName = n.name;
    const std::string partId = machine.holdingPart(nozzleId);
    if (partId.empty()) {
        why = "No Part on the current nozzle!";
        return false;
    }
    std::string feederId, feederName, type;
    std::optional<JPLocation> pick;
    double partHeight = 0;
    bool heightAbove = false, coverOpen = true;
    const std::optional<JPLocation> cameraAt = machine.cameraLocation();
    main([&] {
        feederId = feederFor(config, partId, cameraAt);
        const JPFeeder* f = config.feeder(feederId);
        if (!f) return;
        feederName = f->name();
        type = f->feedsAs();
        pick = f->pickLocation();
        heightAbove = f->partHeightAbovePickLocation();
        if (const JPPart* p = config.part(partId)) partHeight = p->height.convertToUnits(JPLengthUnit::Millimeters).value();
        if (type == "BlindsFeeder") coverOpen = JPBlindsFeeders::coverState(*f, true);
    });
    // None that can take it: as OpenPnP's, nothing is done.
    if (feederId.empty()) return true;
    JJson g = JJson::object();
    g["nozzle"] = nozzleName;
    g["feeder"] = feederName;
    g["part"] = partId;
    if (scripting && !scripting->on("Feeder.BeforeTakeBack", g, why)) return false;
    if (type == "ReferenceHeapFeeder") {
        if (!JPHeapFeeder::takeBack(config, feederId, nozzleId, machine, onMain, why)) return false;
    } else {
        // A blinds feeder whose cover is not open: its last feed again, so the pick place is the last free pocket.
        if (type == "BlindsFeeder" && !coverOpen) {
            main([&] {
                if (JPFeeder* f = config.feeder(feederId)) f->setNumber("feed-count", f->number("feed-count") - 1);
            });
            bool empty = false;
            if (!JPFeederFeed::feed(config, feederId, nozzleId, machine, onMain, why, empty)) return false;
            main([&] {
                if (const JPFeeder* f = config.feeder(feederId)) pick = f->pickLocation();
            });
        }
        if (!pick) {
            why = "Feeder: " + feederName + ", Not ready to take back the part (e.g. no free slot)";
            return false;
        }
        // OpenPnP's putPartBack: at the last pick's place, let go, up to safe Z, checked gone as the tip says.
        JPLocation at = *pick;
        if (heightAbove) at = at.add(JPLocation(at.units(), 0, 0, JPLength(partHeight, JPLengthUnit::Millimeters).convertToUnits(at.units()).value(), 0));
        if (!machine.place(nozzleId, at, why)) {
            why = "Feeder: " + feederName + ", Putting part back failed, check nozzle tip (" + why + ")";
            return false;
        }
        machine.holding(nozzleId, "");
        if (!machine.safeZ(why)) return false;
        bool off = true;
        if (machine.vacuumChecked(nozzleId, JPJobMachine::VacuumStep::AfterPlace) && (!machine.partOff(nozzleId, off, why) || !off)) {
            why = "Feeder: " + feederName + ", Putting part back failed, check nozzle tip" + (off ? " (" + why + ")" : std::string());
            return false;
        }
    }
    main([&] {
        if (JPFeeder* f = config.feeder(feederId)) f->partTakenBack();
    });
    return !scripting || scripting->on("Feeder.AfterTakeBack", g, why);
}

} // inline namespace jf
