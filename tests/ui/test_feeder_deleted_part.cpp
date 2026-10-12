// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A feeder's part on the Feeders tab (JPFeedersTableModel): by its Name, never its id; once the part is deleted
// from the library, said so; no part, nothing.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "ui/JPFeedersTableModel.h"

#include <filesystem>

using namespace jf;

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "jplacer-test-feeder-deleted-part";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    JPConfiguration config(dir.string());
    auto part = std::make_shared<JPPart>();
    part->id = "6a1f0b2e-0000-4000-8000-000000000001";
    part->label = "C0603-100n";
    config.addPart(part);
    config.addFeeder(JPFeeder::create("org.openpnp.machine.reference.feeder.ReferenceStripFeeder", part->id));
    config.addFeeder(JPFeeder::create("org.openpnp.machine.reference.feeder.ReferenceStripFeeder", ""));
    JPFeedersTableModel model(config);
    int partColumn = -1;
    for (int c = 0; c < model.columnCount(); ++c)
        if (model.column(c).name == "Part") partColumn = c;
    assert(partColumn >= 0 && model.rowCount() == 2);
    assert(model.text(0, partColumn) == "C0603-100n" && model.text(1, partColumn).empty());
    config.removePart(part->id);
    assert(model.text(0, partColumn) == JPFeeder::kDeletedPart);
    std::filesystem::remove_all(dir);
    return 0;
}
