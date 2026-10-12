// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The part a head's homing fiducial is looked for as (JPVisualHoming::homePart): its Homing Fiducial Part, by its
// id, whatever it is named (renamed, still it); none chosen, the part named FIDUCIAL-HOME, as OpenPnP has it;
// one chosen and since deleted, none. The head keeps its choice in its file (JPHeadConfig).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "tasks/JPVisualHoming.h"

#include <filesystem>

using namespace jf;

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "jplacer-test-homing-part";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    JPConfiguration config(dir.string());
    auto home = std::make_shared<JPPart>();
    home->label = "FIDUCIAL-HOME";
    config.addPart(home);
    auto mark = std::make_shared<JPPart>();
    mark->label = "HOME-MARK-2";
    config.addPart(mark);

    JPHeadConfig head;
    assert(JPVisualHoming::homePart(config, head) == home.get());   // none chosen: by OpenPnP's name
    head.homingFiducialPart = mark->id;
    assert(JPVisualHoming::homePart(config, head) == mark.get());
    mark->label = "Renamed";   // by its id: renamed, still it
    assert(JPVisualHoming::homePart(config, head) == mark.get());
    // Kept with the head, and read back; one not chosen not written.
    const JPHeadConfig back = JPHeadConfig::fromJson(JJson::parse(head.toJson().dump()));
    assert(back.homingFiducialPart == mark->id);
    assert(!JPHeadConfig().toJson().contains("homingFiducialPart"));
    config.removePart(mark->id);
    assert(!JPVisualHoming::homePart(config, head));   // chosen and gone: none (not another by chance)
    std::filesystem::remove_all(dir);
    return 0;
}
