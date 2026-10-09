// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// How a fiducial is looked at: by its vision settings' OpenPnP pipeline (as OpenPnP finds fiducials), prepared for
// its part (its footprint, its diameter, where to look, the settings' parameter values) and the averaging the
// machine asks for.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPVisionPipelines.h"
#include "tasks/JPFiducialLocator.h"

#include <cmath>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-fiducial-pipeline";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    auto pkg = std::make_shared<JPPackage>();
    pkg->id = "FID-1.5";
    pkg->footprint.pads.push_back({ "1", 0, 0, 1.5, 1.5, 0, false, 100 });
    config.addPackage(pkg);
    auto part = std::make_shared<JPPart>();
    part->id = "FID";
    part->packageId = pkg->id;
    config.addPart(part);
    JPVisionSettings fvs = JPVisionSettings::create(JPVisionSettings::Kind::Fiducial, "FVS_Default");
    JPVisionPipelines::assign(fvs, "maxDistance", JPPipelineValue { JPPipelineValue::LengthMm { 2.0 } });
    config.addVisionSettings(fvs);

    JPVisionConfig vision;
    double diameter = 0;
    JPJobMachine::FiducialLook look;
    std::string settings;
    // By its pipeline (fiducials are always), averaging: the stock fiducial pipeline, prepared for FID.
    vision.enabledAveraging = true;
    assert(JPFiducialLocator::partLook(config, *part, vision, diameter, look, settings) == JPFiducialLocator::PartProblem::None);
    assert(std::abs(diameter - 1.5) < 1e-9 && look.pipeline && look.partId == "FID" && look.averaging);
    JPPipeline& p = *look.pipeline;
    assert(p.stage("results") && p.context().configurationDirectory == dir.string());
    assert(std::abs(std::get<JPPipelineValue::LengthMm>(p.property("fiducial.diameter")->value).mm - 1.5) < 1e-9);
    assert(std::get<JPPipelineValue::Part>(p.property("part")->value).packageId == "FID-1.5");
    // The settings' values given; the stock pipeline has a maxDistance stage, so no fallback.
    assert(std::abs(std::get<JPPipelineValue::LengthMm>(p.property("maxDistance")->value).mm - 2.0) < 1e-9);
    assert(!p.property("fiducial.maxDistance"));
    fs::remove_all(dir);
    return 0;
}
