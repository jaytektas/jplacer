// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

// A job machine that does nothing and refuses what needs a camera: every call there for a test to override what it
// needs. Its nozzle moves are kept; it is homed unless told otherwise.

#include "tasks/JPJobMachine.h"

#include <string>
#include <vector>

class FakeJobMachine : public jf::JPJobMachine {
public:
    using JPLocation = jf::JPLocation;
    using JPPipeline = jf::JPPipeline;

    bool homed = true;
    std::vector<JPLocation> positioned;   // where nozzles were put

    std::vector<Nozzle> nozzles() const override { return {}; }
    std::vector<std::pair<std::string, std::string>> tips() const override { return {}; }
    void setRotationModeOffset(const std::string&, std::optional<double>) override {}
    std::optional<JPLocation> cameraLocation() const override { return std::nullopt; }
    bool cameraReaches(const JPLocation&) const override { return true; }
    TipPush tipPush(const std::string&) const override { return { true, 1.0 }; }
    std::string holdingPart(const std::string&) const override { return {}; }
    std::string chosenNozzle() const override { return {}; }
    bool positionActuator(const std::string&, std::array<std::optional<double>, 4>, double, bool, std::string&) override { return true; }
    bool zeroActuatorRotation(const std::string&, std::string&) override { return true; }
    bool isHomed() const override { return homed; }
    bool safeZ(std::string&) override { return true; }
    bool changeTip(const std::string&, const std::string&, std::string&) override { return true; }
    bool rotate(const std::string&, double, std::string&) override { return true; }
    bool pick(const std::string&, const JPLocation&, std::string&) override { return true; }
    bool place(const std::string&, const JPLocation&, std::string&) override { return true; }
    bool discard(const std::string&, std::string&) override { return true; }
    bool positionNozzle(const std::string&, const JPLocation& at, std::string&) override {
        positioned.push_back(at);
        return true;
    }
    bool positionCamera(const JPLocation&, std::string&) override { return true; }
    bool moveNozzle(const std::string&, std::array<std::optional<double>, 4>, double, bool, std::string&) override { return true; }
    bool vacuumOn(const std::string&, std::string&) override { return true; }
    bool pickHere(const std::string&, std::string&) override { return true; }
    bool readVacuum(const std::string&, double& level, std::string&) override {
        level = 0;
        return true;
    }
    bool actuate(const std::string&, double, std::string&) override { return true; }
    bool actuateText(const std::string&, const std::string&, std::string&) override { return true; }
    bool readActuator(const std::string& name, const std::string&, std::string&, std::string& why) override {
        why = "Unable to find an actuator named " + name;
        return false;
    }
    bool moveActuator(const std::string&, const JPLocation&, bool, double, std::string&) override { return true; }
    bool matchTemplate(const JPLocation&, const std::string&, const jf::JPTemplateFinder::Area&, JPLocation&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool park(std::string&) override { return true; }
    bool locateFiducial(const JPLocation&, double, const FiducialLook&, JPLocation&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool seeRects(const JPLocation&, JPPipeline&, int, SeenRects&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool seeCircles(const JPLocation&, JPPipeline&, SeenCircles&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool lookThrough(const JPLocation&, JPPipeline&, Sight&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool cameraSight(Sight&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool readQrCodes(const JPLocation&, std::vector<QrCode>&, std::string& why) override {
        why = "no camera";
        return false;
    }
    void showOnCamera(const cv::Mat&, int) override {}
    bool alignPart(const std::string&, const AlignRequest&, AlignResult&, std::string& why) override {
        why = "no camera";
        return false;
    }
    bool locateHole(const JPLocation&, JPPipeline&, double, const std::vector<JPLocation>&, const std::function<void(JPPipeline&)>&,
                    JPLocation&, std::string& why) override {
        why = "no camera";
        return false;
    }
};
