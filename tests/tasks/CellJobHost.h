// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

// What a job run on a cell by a test takes from where it runs (JPJobMachineHost), as the app's machine gives it
// without its window: the cell; with startCameras(), a feed for each of its cameras showing what the simulated
// machine sees (JPCameraSimulation), the part each nozzle holds told to the cell and to Simulation Mode's Pick &
// Place Checking (JPPnpChecking); the tips put on, their runout and background measured with the camera looking
// up and kept, in the cell's settings.

#include "camera/JPCameraFeed.h"
#include "machine/JPCell.h"
#include "model/JPConfiguration.h"
#include "tasks/JPCameraSimulation.h"
#include "tasks/JPJobMachineHost.h"
#include "tasks/JPPnpChecking.h"
#include "tasks/JPRunoutCalibrator.h"

#include <atomic>
#include <cassert>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CellJobHost : public jf::JPJobMachineHost {
public:
    CellJobHost(jf::JPCell& cell, const jf::JPConfiguration& configuration) : m_cell(cell), m_configuration(configuration) {
        // Each check counted, so a test can tell they were made.
        m_cell.setPnpChecker([this, check = m_checking.checker()](const jf::JPCell::PnpCheck& c, std::string& detail) {
            ++(c.pick ? m_picksChecked : m_placesChecked);
            return check(c, detail);
        });
    }
    // Simulation Mode's Pick & Place Checking: how many picks and places it checked.
    int picksChecked() const { return m_picksChecked; }
    int placesChecked() const { return m_placesChecked; }
    ~CellJobHost() override {
        for (auto& f : m_feeds) f->stop();
    }

    // A feed for each camera, started.
    void startCameras() {
        for (const jf::JPCameraConfig& c : m_cell.config().cameras) {
            auto feed = std::make_unique<jf::JPCameraFeed>(c);
            feed->setView(jf::JPCameraSimulation::view(m_cell, c));
            feed->setExtras(jf::JPCameraSimulation::extras(m_cell, c, m_checking.holder()));
            feed->start();
            m_feeds.push_back(std::move(feed));
        }
    }

    jf::JPCell* cell() const override { return &m_cell; }
    std::optional<jf::JPLocation> cameraLocation() const override {
        const jf::JPCameraFeed* f = headCameraFeed();
        if (!f) return std::nullopt;
        const jf::JPMountConfig& m = f->config().mount;
        const auto at = m_cell.jogBase();
        return jf::JPLocation(jf::JPLengthUnit::Millimeters, at.at(m.axisX) + m.offsetX, at.at(m.axisY) + m.offsetY, 0, 0);
    }
    jf::JPCameraFeed* headCameraFeed() const override {
        for (const auto& f : m_feeds)
            if (!f->config().mount.axisX.empty() && !f->config().mount.axisY.empty()) return f.get();
        return nullptr;
    }
    jf::JPCameraFeed* upCameraFeed() const override {
        for (const auto& f : m_feeds)
            if (f->config().mount.headId.empty() && f->config().looksUp) return f.get();
        return nullptr;
    }
    jf::JPCameraFeed* cameraFeed(const std::string& idOrName) const override {
        for (const auto& f : m_feeds)
            if (f->config().id == idOrName || f->config().name == idOrName) return f.get();
        return nullptr;
    }
    void showPicture(const jf::JPCameraFeed*, const jf::JPFrame&, const std::string&, int) override {}
    void showCamera(const std::string&) override {}
    std::string nozzlePart(const std::string& nozzleId) const override {
        std::lock_guard lk(m_mutex);
        const auto p = m_parts.find(nozzleId);
        return p == m_parts.end() ? std::string() : p->second;
    }
    void setNozzlePart(const std::string& nozzleId, const std::string& partId) override {
        {
            std::lock_guard lk(m_mutex);
            m_parts[nozzleId] = partId;
        }
        const jf::JPPnpChecking::PartOn part = jf::JPPnpChecking::partOn(&m_configuration, m_cell.config(), nozzleId, partId);
        m_checking.hold(nozzleId, part.held.footprint, part.held.heightMm);
        m_cell.setNozzlePart(nozzleId, part.on);
    }
    std::string chosenNozzleId() const override { return {}; }
    std::string tipChangeRefusal(const std::string&, const std::string&) const override { return {}; }
    void setTipOn(const std::string& nozzleId, const std::string& tipId) override {
        change([&](jf::JPCellConfig& c) {
            for (jf::JPNozzleConfig& n : c.nozzles)
                if (n.id == nozzleId) n.tipId = tipId;
        });
    }
    std::string cellPath() const override { return {}; }
    void slotScored(const std::string&, double) override {}
    std::optional<jf::JPRunout> measureRunout(jf::JPCell& cell, jf::JPCameraFeed& feed, const jf::JPNozzleConfig& nozzle,
                                              const jf::JPNozzleTipConfig& tip, std::string& words,
                                              std::optional<jf::JPBackgroundCalibration::Result>& background) override {
        return jf::JPRunoutCalibrator::measure(cell, feed, nozzle, tip, nullptr, words, nullptr, background);
    }
    void keepRunout(const std::string& tipId, const std::string& nozzleId, const std::optional<jf::JPRunout>& runout,
                    const std::optional<jf::JPBackgroundCalibration::Result>& b) override {
        change([&](jf::JPCellConfig& c) {
            for (jf::JPNozzleTipConfig& t : c.nozzleTips)
                if (t.id == tipId) {
                    if (runout) t.runout[nozzleId] = *runout;
                    else t.runout.erase(nozzleId);
                    if (b) {
                        jf::JPNozzleTipConfig::Background& g = t.background;
                        g.minHue = b->minHue;
                        g.maxHue = b->maxHue;
                        g.minSaturation = b->minSaturation;
                        g.maxSaturation = b->maxSaturation;
                        g.minValue = b->minValue;
                        g.maxValue = b->maxValue;
                        g.diagnostics = b->diagnostics;
                    }
                }
        });
    }

private:
    template <typename Edit> void change(Edit edit) {
        jf::JPCellConfig c = m_cell.config();
        edit(c);
        std::string why;
        const bool ok = m_cell.reconfigure(c, why);
        assert(ok);
    }

    jf::JPCell&                                    m_cell;
    const jf::JPConfiguration&                     m_configuration;
    jf::JPPnpChecking                              m_checking;
    std::vector<std::unique_ptr<jf::JPCameraFeed>> m_feeds;
    mutable std::mutex                             m_mutex;
    std::map<std::string, std::string>             m_parts;
    std::atomic<int>                               m_picksChecked { 0 }, m_placesChecked { 0 };
};
