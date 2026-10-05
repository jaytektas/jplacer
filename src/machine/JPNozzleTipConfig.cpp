// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPNozzleTipConfig.h"

inline namespace jf {

namespace {

using Kind = JPChangerStep::Kind;

// Where the nozzle is after a move or safe Z step: the move's coordinates,
// the ones it leaves out as they were; up at safe Z after a safe Z step.
struct Place {
    std::optional<double> x, y, z, rotation;
    bool safe = false;
};

JPChangerStep moveTo(const Place& p, double speed) {
    JPChangerStep s;
    s.kind = Kind::Move;
    s.x = p.x;
    s.y = p.y;
    s.z = p.z;
    s.rotation = p.rotation;
    s.speed = speed;
    return s;
}

JJson toArray(const std::vector<JPChangerStep>& steps) {
    JJson a = JJson::array();
    for (const JPChangerStep& s : steps) a.push(s.toJson());
    return a;
}

void problemsOf(const std::string& what, const std::vector<JPChangerStep>& steps, std::vector<std::string>& out) {
    if (steps.empty()) return;
    const JPChangerStep& first = steps.front();
    if (first.kind != Kind::Move || !first.x || !first.y || !first.z)
        out.push_back(what + " must start with a move giving X, Y and Z");
    for (const JPChangerStep& s : steps)
        if (s.kind == Kind::Actuator && s.actuatorId.empty()) out.push_back(what + " switch an actuator not named");
}

} // namespace

std::vector<JPChangerStep> JPNozzleTipConfig::reversed(const std::vector<JPChangerStep>& steps) {
    auto moves = [](const JPChangerStep& s) { return s.kind == Kind::Move || s.kind == Kind::SafeZ; };
    // places[i]: where the nozzle is once step i is done.
    std::vector<Place> places(steps.size());
    Place at;
    size_t last = steps.size();   // the last move
    for (size_t i = 0; i < steps.size(); ++i) {
        const JPChangerStep& s = steps[i];
        if (s.kind == Kind::Move) {
            if (s.x) at.x = s.x;
            if (s.y) at.y = s.y;
            if (s.z) at.z = s.z;
            if (s.rotation) at.rotation = s.rotation;
            at.safe = false;
            last = i;
        } else if (s.kind == Kind::SafeZ) {
            at.safe = true;
        }
        places[i] = at;
    }
    std::vector<JPChangerStep> out;
    if (last == steps.size()) return out;
    // Unloading comes in to where loading's last move went (the approach).
    out.push_back(moveTo(places[last], steps[last].speed));
    for (size_t i = steps.size(); i-- > 0;) {
        const JPChangerStep& s = steps[i];
        if (!moves(s)) {
            JPChangerStep undo = s;
            if (s.kind == Kind::Actuator) undo.on = !s.on;
            out.push_back(undo);
            continue;
        }
        // Up to safe Z after the last move: unloading comes in from there.
        if (i > last) continue;
        // Undone by going back to where it started; the first move came in
        // from safe Z, where unloading ends anyway.
        size_t before = i;
        while (before-- > 0 && !moves(steps[before])) {}
        if (before == size_t(-1)) continue;
        const Place& from = places[before];
        if (from.safe && s.kind == Kind::SafeZ) continue;   // up from up: nothing to undo
        if (from.safe) {
            JPChangerStep up;
            up.kind = Kind::SafeZ;
            up.speed = s.speed;
            out.push_back(up);
            Place across = from;
            across.z.reset();
            out.push_back(moveTo(across, s.speed));
        } else {
            out.push_back(moveTo(from, s.speed));
        }
    }
    return out;
}

std::vector<JPChangerStep> JPNozzleTipConfig::unloadingSteps() const {
    return unloadReversesLoad ? reversed(loadSteps) : unloadSteps;
}

std::vector<std::string> JPNozzleTipConfig::problems() const {
    std::vector<std::string> out;
    const std::string tip = "nozzle tip " + (name.empty() ? id : name) + ": ";
    problemsOf(tip + "its load steps", loadSteps, out);
    if (!unloadReversesLoad) problemsOf(tip + "its unload steps", unloadSteps, out);
    return out;
}

JPNozzleTipConfig JPNozzleTipConfig::fromJson(const JJson& j) {
    JPNozzleTipConfig t;
    t.id       = j["id"].str();
    t.name     = j["name"].str();
    t.diameter = j["diameter"].number();
    for (const JJson& s : j["load"].arr()) t.loadSteps.push_back(JPChangerStep::fromJson(s));
    t.unloadReversesLoad = j["unloadReversesLoad"].boolean(true);
    for (const JJson& s : j["unload"].arr()) t.unloadSteps.push_back(JPChangerStep::fromJson(s));
    t.maxPartDiameterMm  = j["maxPartDiameterMm"].number(t.maxPartDiameterMm);
    t.maxPickToleranceMm = j["maxPickToleranceMm"].number(t.maxPickToleranceMm);
    t.pushAndDragAllowed = j["pushAndDragAllowed"].boolean();
    t.diameterLowMm      = j["diameterLowMm"].number(t.diameterLowMm);
    t.pickDwellMs  = int(j["pickDwellMs"].number());
    t.placeDwellMs = int(j["placeDwellMs"].number());
    for (const auto& [key, sensing] : { std::pair{ "partOn", &t.partOn }, std::pair{ "partOff", &t.partOff } }) {
        const JJson& d = j[key];
        if (!d.isObject()) continue;
        if (const std::string& m = d["method"].str(); !m.empty()) sensing->method = m;
        sensing->low      = d["low"].number(0.0);
        sensing->high     = d["high"].number(0.0);
        sensing->diffLow  = d["diffLow"].number(0.0);
        sensing->diffHigh = d["diffHigh"].number(0.0);
    }
    t.partOffProbingMs = int(j["partOff"]["probingMs"].number());
    t.partOffDwellMs   = int(j["partOff"]["dwellMs"].number());
    if (const JJson& k = j["runoutCalibration"]; k.isObject()) {
        t.runoutCalibration.enabled        = k["enabled"].boolean();
        t.runoutCalibration.divisions      = int(k["divisions"].number(t.runoutCalibration.divisions));
        t.runoutCalibration.misdetects     = int(k["misdetects"].number(0));
        t.runoutCalibration.zOffset        = k["zOffset"].number(0.0);
        t.runoutCalibration.visionDiameter = k["visionDiameter"].number(0.0);
    }
    if (const JJson& r = j["runout"]; r.isObject())
        for (const auto& [nozzle, v] : r.obj()) t.runout[nozzle] = JPRunout::fromJson(v);
    return t;
}

JJson JPNozzleTipConfig::toJson() const {
    JJson j = JJson::object();
    j["id"]       = id;
    j["name"]     = name;
    j["diameter"] = diameter;
    j["load"]     = toArray(loadSteps);
    j["unloadReversesLoad"] = unloadReversesLoad;
    j["unload"]   = toArray(unloadSteps);
    j["maxPartDiameterMm"]  = maxPartDiameterMm;
    j["maxPickToleranceMm"] = maxPickToleranceMm;
    if (pushAndDragAllowed) j["pushAndDragAllowed"] = true;
    j["diameterLowMm"]      = diameterLowMm;
    if (pickDwellMs) j["pickDwellMs"] = pickDwellMs;
    if (placeDwellMs) j["placeDwellMs"] = placeDwellMs;
    for (const auto& [key, sensing] : { std::pair{ "partOn", &partOn }, std::pair{ "partOff", &partOff } }) {
        j[key]["method"]   = sensing->method;
        j[key]["low"]      = sensing->low;
        j[key]["high"]     = sensing->high;
        j[key]["diffLow"]  = sensing->diffLow;
        j[key]["diffHigh"] = sensing->diffHigh;
    }
    j["partOff"]["probingMs"] = partOffProbingMs;
    j["partOff"]["dwellMs"]   = partOffDwellMs;
    j["runoutCalibration"]["enabled"]        = runoutCalibration.enabled;
    j["runoutCalibration"]["divisions"]      = runoutCalibration.divisions;
    j["runoutCalibration"]["misdetects"]     = runoutCalibration.misdetects;
    j["runoutCalibration"]["zOffset"]        = runoutCalibration.zOffset;
    j["runoutCalibration"]["visionDiameter"] = runoutCalibration.visionDiameter;
    if (!runout.empty())
        for (const auto& [nozzle, r] : runout) j["runout"][nozzle] = r.toJson();
    return j;
}

} // inline namespace jf
