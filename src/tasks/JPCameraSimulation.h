// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "camera/JPSimulatedSource.h"
#include "machine/JPCameraConfig.h"
#include "machine/JPCell.h"
#include "tasks/JPPnpChecking.h"

#include <functional>

inline namespace jf {

// What a simulated camera of the cell sees (a camera feed's view and extras, JPCameraFeed::setView and
// setExtras), as OpenPnP's simulated machine shows it: a head camera looks where its axes put it, plus its
// offset on the head and the correction visual homing made (the world stays where the switches put it), seen
// through Simulation Mode's imperfections (JPSimulatedViewpoint); a fixed one from where it is. A fixed camera
// sees the nozzle tips over it where their axes put them (as OpenPnP's simulation: the homing error is what a camera
// looking down sees), going round on the runout, with the part each holds; Simulation Mode adds
// sparks of noise, and dark while its light is off. OpenPnP's own SimulatedUpCamera shows the nozzles whether
// or not the machine is in Simulation Mode, from its Camera Location.
class JPCameraSimulation {
public:
    using View = std::function<bool(double& x, double& y)>;
    static View view(JPCell& cell, const JPCameraConfig& camera);
    static JPSimulatedSource::ExtrasProvider extras(JPCell& cell, const JPCameraConfig& camera, JPPnpChecking::Holder holding);
    // A simulated nozzle tip seen from below: this wide when its tip gives no diameter (mm).
    static constexpr double kTipMm = 1.0;
};

} // inline namespace jf
