// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's ReferenceBottomVisionInheritanceTest: a part's bottom vision settings are its own, else its package's,
// else the machine's default; Generalize takes a package's parts' own settings away so they share the package's;
// and settings given the default's values (Reset) are the default's.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

#include "machine/JPVisionConfig.h"
#include "model/JPConfiguration.h"
#include "setup/JPVisionForms.h"
#include "setup/JPVisionPipelines.h"

using namespace jf;
namespace fs = std::filesystem;

namespace {

using Kind = JPVisionSettings::Kind;

struct Bench {
    JPConfiguration config;
    JPVisionConfig  machine;   // OpenPnP's ReferenceBottomVision: its default settings

    explicit Bench(const std::string& dir) : config(dir) {
        std::vector<std::string> problems;
        std::string error;
        assert(config.load(problems, error));
        JPVisionPipelines::ensureStock(config);
    }
    JPVisionSettings& machineDefault() { return *config.visionSettings(machine.bottomVisionId); }
    const JPVisionSettings& inherited(const std::string& partId) {
        const JPVisionSettings* v = config.inheritedVision(*config.part(partId), Kind::Bottom, machine.bottomVisionId);
        assert(v);
        return *v;
    }
    // OpenPnP's new BottomVisionSettings().
    std::string added(const std::string& id) {
        config.addVisionSettings(JPVisionSettings::create(Kind::Bottom, id));
        return id;
    }
    void assertIsDefault(const JPVisionSettings& v) {
        const JPVisionSettings& d = machineDefault();
        assert(v.enabled == d.enabled);
        for (const char* a : { "pre-rotate-usage", "check-part-size-method", "check-size-tolerance-percent", "max-rotation" })
            assert(v.text(a) == d.text(a));
        assert(v.locationOf("vision-offset") == d.locationOf("vision-offset"));
    }
};

void testBottomVisionSettingsInheritance(const std::string& dir) {
    Bench b(dir);
    JPPart& part = *b.config.part("R0805-1K");
    JPPackage& pkg = *b.config.package(part.packageId);
    assert(part.bottomVisionId.empty());   // Part Bottom Vision should be null
    assert(pkg.bottomVisionId.empty());    // Part Package Bottom Vision should be null
    b.assertIsDefault(b.inherited(part.id));

    const std::string custom = b.added("BVS_Custom");
    b.config.visionSettings(custom)->enabled = false;
    pkg.bottomVisionId = custom;
    assert(part.bottomVisionId.empty());
    assert(!b.inherited(part.id).enabled);   // Part should inherit BottomVisionSettings from Package

    b.machineDefault().setText("pre-rotate-usage", "AlwaysOn");
    assert(b.inherited(part.id).text("pre-rotate-usage") == "Default");   // from the package's custom settings

    pkg.bottomVisionId.clear();
    assert(b.inherited(part.id).text("pre-rotate-usage") == "AlwaysOn");   // the machine's default again
}

void testBottomVisionReset(const std::string& dir) {
    Bench b(dir);
    JPPart& part1 = *b.config.part("R0805-1K");
    JPPart& part2 = *b.config.part("R0805-2K");
    JPPackage& pkg = *b.config.package(part1.packageId);

    const std::string packageSettings = b.added("BVS_Package");
    b.config.visionSettings(packageSettings)->setText("pre-rotate-usage", "AlwaysOn");
    b.config.visionSettings(packageSettings)->setText("max-rotation", "Full");
    pkg.bottomVisionId = packageSettings;

    const std::string partSettings = b.added("BVS_Part");
    b.config.visionSettings(partSettings)->setText("max-rotation", "Adjust");
    part1.bottomVisionId = partSettings;

    // Generalize on the package.
    std::string why;
    assert(JPVisionForms::act(b.config, packageSettings, "generalize", { JPVisionForms::Holder::Kind::Package, pkg.id },
                              b.machine.bottomVisionId, why));
    assert(part1.bottomVisionId.empty() && part2.bottomVisionId.empty());
    for (const char* id : { "R0805-1K", "R0805-2K" }) {
        assert(b.inherited(id).text("pre-rotate-usage") == "AlwaysOn");   // inherited from the package
        assert(b.inherited(id).text("max-rotation") == "Full");
    }

    // The package's settings given the machine default's values (its Reset to Default).
    assert(JPVisionForms::act(b.config, packageSettings, "reset", { JPVisionForms::Holder::Kind::Package, pkg.id },
                              b.machine.bottomVisionId, why));
    b.assertIsDefault(b.inherited("R0805-1K"));
    b.assertIsDefault(b.inherited("R0805-2K"));
    assert(b.config.visionSettings(packageSettings)->id == packageSettings);   // its id kept
}

} // namespace

int main() {
    // OpenPnP's test configuration (ReferenceBottomVisionOffset's parts and packages) in a configuration of its own.
    const fs::path dir = fs::temp_directory_path() / ("jplacer-bottom-vision-inheritance-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    for (const char* f : { "packages.xml", "parts.xml" })
        fs::copy_file(fs::path(JPLACER_TESTDATA_DIR) / "openpnp/bottom-vision-offset" / f, dir / f, fs::copy_options::overwrite_existing);
    testBottomVisionSettingsInheritance(dir.string());
    testBottomVisionReset(dir.string());
    fs::remove_all(dir);
    return 0;
}
