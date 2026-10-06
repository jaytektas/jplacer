// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPCameraSimulation.h"

#include "camera/JPSimulatedUpCamera.h"
#include "camera/JPSimulatedViewpoint.h"
#include "model/JPLength.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

namespace {

// Where a tool on the head physically is: its axes, its offset, and the
// correction visual homing made (the world stays where the switches put it).
bool physical(const JPCell& cell, const JPMountConfig& m, double& x, double& y) {
    const auto p = cell.positions();
    const auto px = p.find(m.axisX), py = p.find(m.axisY);
    if (px == p.end() || py == p.end()) return false;
    const auto corrected = cell.correctionSinceHome();
    const auto cx = corrected.find(m.axisX), cy = corrected.find(m.axisY);
    x = px->second + m.offsetX + (cx == corrected.end() ? 0 : cx->second);
    y = py->second + m.offsetY + (cy == corrected.end() ? 0 : cy->second);
    return true;
}

// Where a tool is by its axes alone: as OpenPnP's simulation has a nozzle over a camera looking up (the homing error
// shifts only what a camera looking down sees, so visual homing's correction is not the nozzle's).
bool atAxes(const JPCell& cell, const JPMountConfig& m, double& x, double& y) {
    const auto p = cell.positions();
    const auto px = p.find(m.axisX), py = p.find(m.axisY);
    if (px == p.end() || py == p.end()) return false;
    x = px->second + m.offsetX;
    y = py->second + m.offsetY;
    return true;
}

// Where OpenPnP's own SimulatedUpCamera is (its Camera Location, else where it is mounted).
struct Place {
    bool                          openPnpUp = false;
    JPSimulatedUpCamera::Settings up;
    double                        x = 0, y = 0, z = 0;
};

Place placeOf(const JPCameraConfig& c) {
    Place p;
    p.openPnpUp = JPSimulatedUpCamera::is(c.device);
    p.up = JPSimulatedUpCamera::Settings::fromDevice(c.device);
    p.x = p.up.location ? p.up.location->x : c.mount.offsetX;
    p.y = p.up.location ? p.up.location->y : c.mount.offsetY;
    p.z = p.up.location ? p.up.location->z : c.mount.offsetZ;
    return p;
}

} // namespace

JPCameraSimulation::View JPCameraSimulation::view(JPCell& cell, const JPCameraConfig& c) {
    if (!c.mount.axisX.empty() && !c.mount.axisY.empty())
        return [cell = &cell, m = c.mount, seen = std::make_shared<JPSimulatedViewpoint>()](double& x, double& y) {
            if (!physical(*cell, m, x, y)) return false;
            if (const JPSimulationConfig sim = cell->simulation(); sim.on()) seen->look(sim, JPSimulatedViewpoint::Clock::now(), x, y);
            return true;
        };
    // A fixed camera looks from where it is, at the nozzle tips over it (simulated).
    return [cell = &cell, at = c.mount, place = placeOf(c)](double& x, double& y) {
        if (!place.openPnpUp && !cell->simulation().on()) return false;
        x = place.openPnpUp ? place.x : at.offsetX;
        y = place.openPnpUp ? place.y : at.offsetY;
        return true;
    };
}

JPSimulatedSource::ExtrasProvider JPCameraSimulation::extras(JPCell& cell, const JPCameraConfig& c, JPPnpChecking::Holder holding) {
    // The nozzle tips the up-looking cameras see.
    struct SimulatedTip {
        std::string   nozzleId;
        JPMountConfig mount;
        double        diameter;
    };
    std::vector<SimulatedTip> tips;
    const JPCellConfig config = cell.config();
    for (const JPNozzleConfig& n : config.nozzles) {
        double diameter = kTipMm;
        for (const JPNozzleTipConfig& t : config.nozzleTips)
            if (t.id == n.tipId && t.diameter > 0) diameter = t.diameter;
        tips.push_back({ n.id, n.mount, diameter });
    }
    return [cell = &cell, tips, fixed = c.mount.headId.empty(), light = c.lightActuator(), place = placeOf(c),
            holding = std::move(holding)] {
        JPSimulatedSource::Extras e;
        const JPSimulationConfig sim = cell->simulation();
        if (!sim.on() && !place.openPnpUp) return e;
        if (sim.dynamic()) {
            e.sparks = sim.cameraNoise;
            e.dark = !light.empty() && !cell->switchedOn(light).value_or(false);
        }
        if (!fixed) return e;
        const auto p = cell->positions();
        for (const SimulatedTip& t : tips) {
            double x, y;
            if (!atAxes(*cell, t.mount, x, y)) continue;
            const auto r = p.find(t.mount.axisRotation);
            const double axis = r == p.end() ? 0.0 : r->second;
            if (sim.dynamic() && sim.runoutMm != 0) {
                const double a = (axis - sim.runoutPhaseDeg) * M_PI / 180;
                x += sim.runoutMm * std::cos(a);
                y += sim.runoutMm * std::sin(a);
            }
            const auto zAxis = p.find(t.mount.axisZ);
            const double tipZ = zAxis == p.end() ? place.z : zAxis->second + t.mount.offsetZ;
            const JPSimulatedUpCamera::Nozzle nozzle { x, y, tipZ, t.diameter, axis + cell->rotationModeOffset(t.nozzleId) };
            // The part on it, as the footprint has it, in mm.
            const JPPnpChecking::Held held = holding ? holding(t.nozzleId) : JPPnpChecking::Held {};
            std::optional<JPSimulatedUpCamera::Part> part;
            if (held.footprint) {
                const double mm = JPLength(1, held.footprint->units).convertToUnits(JPLengthUnit::Millimeters).value();
                auto inMm = [mm](const JPFootprint::Outline& o) {
                    JPSimulatedUpCamera::Polygon out;
                    for (const JPFootprint::Point& pt : o) out.push_back({ pt.x * mm, pt.y * mm });
                    return out;
                };
                part = JPSimulatedUpCamera::Part { inMm(held.footprint->bodyOutline()), {}, held.heightMm };
                for (const JPFootprint::Outline& o : held.footprint->padsOutlines()) part->pads.push_back(inMm(o));
            }
            JPSimulatedUpCamera::drawNozzle(place.openPnpUp ? &place.up : nullptr, place.x, place.y, place.z, nozzle,
                                            part ? &*part : nullptr, e);
        }
        return e;
    };
}

} // inline namespace jf
