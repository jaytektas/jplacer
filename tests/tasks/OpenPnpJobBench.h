// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

// OpenPnP's job tests' machine: one of its test configurations (machine.xml, parts, packages) brought into jplacer as
// an OpenPnP import brings it (the cell, the feeders), made as fast as its tests make it (SampleJobTest's
// makeMachineFastest: every axis 1000000 mm/s at 2000000 mm/s^2), and a job of it run by jplacer's job processor on a
// machine that does each step at once, each plan the processor makes kept (OpenPnP's PlannerStepResults).

#include "FakeJobMachine.h"
#include "machine/JPCellConfig.h"
#include "model/JPConfiguration.h"
#include "openpnp/JPOpenPnpMachineImporter.h"
#include "tasks/JPJobProcessor.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

class OpenPnpJobBench {
public:
    using PlannedPlacement = jf::JPJobProcessor::PlannedPlacement;

    // The machine the job runs on: the cell's nozzles, with no tip loaded (as OpenPnP's test machines start).
    class Machine : public FakeJobMachine {
    public:
        std::vector<Nozzle>                              heads;
        std::vector<std::pair<std::string, std::string>> nozzleTips;
        std::optional<jf::JPTravel::Cost>                travel;
        std::vector<Nozzle> nozzles() const override { return heads; }
        std::vector<std::pair<std::string, std::string>> tips() const override { return nozzleTips; }
        std::optional<jf::JPTravel::Cost> travelCost() const override { return travel; }
        bool changeTip(const std::string& nozzleId, const std::string& tipId, std::string&) override {
            for (Nozzle& n : heads)
                if (n.id == nozzleId) n.tipId = tipId;
            return true;
        }
    };

    std::filesystem::path                 dir;
    std::unique_ptr<jf::JPConfiguration>  config;
    jf::JPCellConfig                      cell;
    Machine                               machine;
    std::unique_ptr<jf::JPJob>            job;
    jf::JPJobProcessorConfig              settings;
    jf::JPVisionConfig                    vision;
    std::vector<std::vector<PlannedPlacement>> results;   // each plan, in turn

    // OpenPnP's configuration in `source` (machine.xml, parts.xml, packages.xml) and its job `jobFile` there.
    OpenPnpJobBench(const std::filesystem::path& source, const std::string& jobFile) {
        namespace fs = std::filesystem;
        static int count = 0;
        dir = fs::temp_directory_path() / ("jplacer-openpnp-job-" + std::to_string(::getpid()) + "-" + std::to_string(count++));
        fs::remove_all(dir);
        fs::create_directories(dir);
        for (const char* f : { "parts.xml", "packages.xml" }) fs::copy_file(source / f, dir / f);
        config = std::make_unique<jf::JPConfiguration>(dir.string());
        std::vector<std::string> problems, notes;
        std::string error;
        bool ok = config->load(problems, error) && config->importFeeders((source / "machine.xml").string(), error) >= 0;
        ok = ok && jf::JPOpenPnpMachineImporter::import((source / "machine.xml").string(), cell, notes, error);
        if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
        assert(ok);
        settings = cell.jobProcessor;
        vision = cell.vision;
        for (const jf::JPNozzleConfig& n : cell.nozzles) {
            Machine::Nozzle m { n.id, n.name, "", n.tipIds };
            m.offsetX = n.mount.offsetX;
            m.offsetY = n.mount.offsetY;
            machine.heads.push_back(m);
        }
        for (const jf::JPNozzleTipConfig& t : cell.nozzleTips) machine.nozzleTips.push_back({ t.id, t.name });
        // SampleJobTest.makeMachineFastest.
        machine.travel = jf::JPTravel::Cost { { 1000000, 2000000 }, { 1000000, 2000000 }, jf::JPTravel::Axis { 1000000, 2000000 } };
        job = config->loadJob((source / jobFile).string(), error);
        if (!job) std::fprintf(stderr, "%s\n", error.c_str());
        assert(job);
    }
    ~OpenPnpJobBench() { std::filesystem::remove_all(dir); }

    // The job run to its end; false, and the failure, where it stops.
    bool run(jf::JPJobProcessor::Failure& failure) {
        jf::JPJobProcessor::Hooks hooks;
        hooks.planned = [this](const std::vector<PlannedPlacement>& step) { results.push_back(step); };
        jf::JPJobProcessor processor(*config, *job, machine, settings, hooks);
        processor.setVision(vision);
        for (;;) {
            const auto r = processor.next(failure);
            if (r == jf::JPJobProcessor::Result::Finished) return true;
            if (r == jf::JPJobProcessor::Result::Failed) return false;
        }
    }
};
