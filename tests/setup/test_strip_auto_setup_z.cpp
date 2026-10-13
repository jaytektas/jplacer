// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A strip feeder's Auto Setup Sets Z?: on for a new strip and one saved before it was there; turned off by the
// Reference Hole Location's Z set on its page (typed, or captured with the nozzle), not by its X or Y nor by the
// same Z set again; ticked again by hand; and kept in jplacer's file.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPStripFeeder.h"
#include "setup/JPFeederForms.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-strip-auto-setup-z";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    std::vector<std::string> problems;
    std::string error;
    assert(config.load(problems, error));

    JPFeeder made = JPFeeder::create(JPStripFeeder::kClass, "");
    assert(made.as<JPStripFeeder>()->autoSetupZ);
    const std::string id = made.id();
    config.addFeeder(std::move(made));
    auto strip = [&] { return config.feeder(id)->as<JPStripFeeder>(); };

    // Saved before the setting was there: on.
    assert(JPStripFeeder::fromJson(JJson::object()).autoSetupZ);

    JPFeederForms::Options options;
    std::map<std::string, bool> clone;
    options.cloneChoices = &clone;
    JPSetupProperties::Form form = JPFeederForms::forFeeder(config, id, nullptr, options);
    assert(form.model.find("reference-hole-location.Z") && form.model.find("auto-setup-z"));

    // X and Y, and the Z it has, leave it on.
    assert(form.model.set("reference-hole-location.X", JVariant(12.5)));
    assert(form.model.set("reference-hole-location.Y", JVariant(40.0)));
    assert(form.model.set("reference-hole-location.Z", JVariant(strip()->referenceHole.convertToUnits(JPLengthUnit::Millimeters).z())));
    assert(strip()->autoSetupZ);

    // A Z of its own: off, and the Z kept.
    assert(form.model.set("reference-hole-location.Z", JVariant(-24.7)));
    assert(!strip()->autoSetupZ);
    assert(std::abs(strip()->referenceHole.convertToUnits(JPLengthUnit::Millimeters).z() + 24.7) < 1e-9);
    assert(!JPFeeder::fromJson(JJson::parse(config.feeder(id)->toJson().dump())).as<JPStripFeeder>()->autoSetupZ);

    // Ticked again by hand.
    assert(form.model.set("auto-setup-z", JVariant(true)));
    assert(strip()->autoSetupZ);
    assert(JPFeeder::fromJson(JJson::parse(config.feeder(id)->toJson().dump())).as<JPStripFeeder>()->autoSetupZ);

    fs::remove_all(dir);
    return 0;
}
