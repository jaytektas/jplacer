// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPRapidScan.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {
constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// A scan's last step rounded up marginally, so the end is included.
constexpr double kEndMargin = 1e-5;
constexpr const char* kRapidClass = "org.openpnp.machine.rapidplacer.RapidFeeder";
}

bool JPRapidScan::scan(JPConfiguration& config, const std::string& feederId, JPJobMachine& machine, const OnMain& onMain, int& found,
                       std::string& why) {
    auto main = [&onMain](const std::function<void()>& fn) {
        if (onMain) onMain(fn);
        else fn();
    };
    JPLocation start(kMm), end(kMm);
    double increment = 0;
    bool known = false;
    main([&] {
        if (const JPFeeder* f = config.feeder(feederId)) {
            known = true;
            start = f->locationOf("scan-start-location").convertToUnits(kMm);
            end = f->locationOf("scan-end-location").convertToUnits(kMm);
            increment = f->lengthOf("scan-increment", JPLength(4, kMm)).convertToUnits(kMm).value();
        }
    });
    if (!known) {
        why = "no feeder " + feederId;
        return false;
    }
    if (increment <= 0) {
        why = "The Scan Increment must be more than 0.";
        return false;
    }
    const double distance = start.linearDistanceTo(end);
    const int last = int(distance / increment + kEndMargin);
    std::vector<JPJobMachine::QrCode> codes;
    for (int i = 0; i <= last; ++i) {
        const double along = distance > 0 ? increment * i / distance : 0;
        const JPLocation at(kMm, start.x() + (end.x() - start.x()) * along, start.y() + (end.y() - start.y()) * along, 0, 0);
        std::vector<JPJobMachine::QrCode> seen;
        std::string none;
        // A place without a feeder is expected: gaps between them.
        if (!machine.readQrCodes(at, seen, none)) {
            JLOGC(JPlacerLog::kJob, JLogLevel::Debug) << "Rapid scan at " << at.text() << ": " << none;
            continue;
        }
        for (const auto& c : seen)
            if (std::none_of(codes.begin(), codes.end(), [&c](const JPJobMachine::QrCode& k) { return k.text == c.text; })) codes.push_back(c);
    }
    main([&] {
        for (const auto& c : codes) {
            JPFeeder* f = nullptr;
            for (JPFeeder& x : config.feeders())
                if (x.typeName() == "RapidFeeder" && x.name() == c.text) {
                    f = &x;
                    break;
                }
            if (!f) {
                JPFeeder fresh = JPFeeder::create(kRapidClass, {});
                fresh.setName(c.text);
                f = &config.addFeeder(std::move(fresh));
            }
            const JPLocation was = f->location();
            f->setLocation(JPLocation(was.units(), c.at.convertToUnits(was.units()).x(), c.at.convertToUnits(was.units()).y(), was.z(), was.rotation()));
            f->setText("address", c.text);
            if (f->partId().empty() && !config.parts().empty()) f->setPartId(config.parts().front()->id);
        }
    });
    found = int(codes.size());
    return true;
}

} // inline namespace jf
