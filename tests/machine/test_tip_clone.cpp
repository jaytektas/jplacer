// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's nozzle tip template cloning: a tip takes the template's changer
// steps, each move moved by how far its own first move is from the
// template's (a coordinate left out stays left out), its touch location too,
// and the Z and vision calibration settings, as chosen; a locked tip, or one
// without a first move to go by, is left as it is.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "machine/JPNozzleTipConfig.h"

using namespace jf;

namespace {

JPChangerStep moveTo(std::optional<double> x, std::optional<double> y, std::optional<double> z) {
    JPChangerStep s;
    s.kind = JPChangerStep::Kind::Move;
    s.x = x;
    s.y = y;
    s.z = z;
    return s;
}

} // namespace

int main() {
    JPNozzleTipConfig templ;
    templ.id = "T1";
    templ.templateTip = true;
    templ.loadSteps = { moveTo(100, 50, -5), moveTo(std::nullopt, 60, -10), moveTo(100, 60, std::nullopt) };
    JPChangerStep wait;
    wait.kind = JPChangerStep::Kind::Wait;
    wait.waitMs = 200;
    templ.loadSteps.insert(templ.loadSteps.begin() + 1, wait);
    templ.unloadReversesLoad = false;
    templ.unloadSteps = { moveTo(100, 60, -10), moveTo(100, 50, -5) };

    JPNozzleTipConfig clone;
    clone.id = "T2";
    clone.loadSteps = { moveTo(120, 50, -6) };   // its slot: 20 mm along X, 1 mm lower
    assert(clone.cloneChangerFrom(templ));
    assert(clone.loadSteps.size() == 4 && clone.loadSteps[1].kind == JPChangerStep::Kind::Wait);
    assert(*clone.loadSteps[0].x == 120 && *clone.loadSteps[0].y == 50 && *clone.loadSteps[0].z == -6);
    assert(!clone.loadSteps[2].x && *clone.loadSteps[2].y == 60 && *clone.loadSteps[2].z == -11);
    assert(*clone.loadSteps[3].x == 120 && !clone.loadSteps[3].z);
    assert(!clone.unloadReversesLoad && *clone.unloadSteps[1].x == 120 && *clone.unloadSteps[0].z == -11);

    // The touch location moved alike, and the Z calibration settings taken;
    // or, as chosen, only one or the other.
    templ.touchLocation = JPMachineLocation { 90, 40, -7, 0 };
    templ.zCalibrationTrigger = "MachineHome";
    templ.zCalibrationFailHoming = false;
    templ.visionCalibration.location = "LastLocation";
    templ.visionCalibration.templateEmpty = "e.png";
    {
        JPNozzleTipConfig both;
        both.loadSteps = { moveTo(120, 50, -6) };
        assert(both.cloneChangerFrom(templ));
        assert(both.touchLocation && both.touchLocation->x == 110 && both.touchLocation->y == 40 && both.touchLocation->z == -8);
        assert(both.zCalibrationTrigger == "MachineHome" && !both.zCalibrationFailHoming);
        assert(both.visionCalibration.location == "LastLocation" && both.visionCalibration.templateEmpty == "e.png");
        JPNozzleTipConfig zOnly;
        zOnly.loadSteps = { moveTo(120, 50, -6) };
        assert(zOnly.cloneChangerFrom(templ, { .locations = false, .zCalibration = true }));
        assert(zOnly.loadSteps.size() == 1 && !zOnly.touchLocation && zOnly.zCalibrationTrigger == "MachineHome");
        JPNozzleTipConfig placesOnly;
        placesOnly.loadSteps = { moveTo(120, 50, -6) };
        assert(placesOnly.cloneChangerFrom(templ, { .locations = true, .zCalibration = false }));
        assert(placesOnly.loadSteps.size() == 4 && placesOnly.touchLocation && placesOnly.zCalibrationTrigger == "Manual");
        JPNozzleTipConfig noVision;
        noVision.loadSteps = { moveTo(120, 50, -6) };
        assert(noVision.cloneChangerFrom(templ, { .locations = true, .zCalibration = true, .visionCalibration = false }));
        assert(!noVision.visionCalibration.on() && noVision.visionCalibration.templateEmpty.empty());
    }

    // Locked: untouched.
    JPNozzleTipConfig locked = clone;
    locked.templateLocked = true;
    locked.loadSteps = { moveTo(140, 50, -5) };
    assert(!locked.cloneChangerFrom(templ) && locked.loadSteps.size() == 1);
    // Nothing to go by: untouched.
    JPNozzleTipConfig bare;
    assert(!bare.cloneChangerFrom(templ) && bare.loadSteps.empty());

    // OpenPnP's four places read from the loading steps brought in from it (First, Third and Last, a Post 1
    // actuator between): what its Vision Calibration's places name; unloading backwards switches the actuator
    // off. A step of jplacer's own: no longer OpenPnP's form.
    {
        JPNozzleTipConfig t;
        assert(t.openPnpChanger() && !t.openPnpChanger()->at[0]);   // no steps: the form, empty
        auto place = [](double x, double y, double z, int slot, double speed) {
            JPChangerStep m = moveTo(x, y, z);
            m.openPnpSlot = slot;
            m.speed = speed;
            return m;
        };
        JPChangerStep post;
        post.kind = JPChangerStep::Kind::Actuator;
        post.actuatorId = "A1";
        post.openPnpSlot = 1;
        t.loadSteps = { place(100, 50, -5, 1, 1), post, place(100, 60, -10, 3, 0.25), place(100, 60, -2, 4, 0.5) };
        const auto back = t.openPnpChanger();
        assert(back && back->at[0] && !back->at[1] && back->at[2]->y == 60 && back->at[3]->z == -2);
        assert(back->post[0] == "A1" && back->post[1].empty() && back->speed[3] == 0.5);
        const auto unload = t.unloadingSteps();
        bool off = false;
        for (const JPChangerStep& u : unload) off = off || (u.kind == JPChangerStep::Kind::Actuator && u.actuatorId == "A1" && !u.on);
        assert(off);
        t.loadSteps.push_back(moveTo(1, 2, 3));   // a step of its own
        assert(!t.openPnpChanger());
    }
    return 0;
}
