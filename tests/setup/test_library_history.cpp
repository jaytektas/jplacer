// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The library's undo and redo (JPLibraryHistory): a change noticed after it is made is a step, named for what
// changed; undo puts the library back (a part changed in place keeps being the same object), redo does it again;
// nothing changed is no step.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPLibraryHistory.h"

#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-library-history";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    for (const char* id : { "R1", "R2", "R3", "C1" }) {
        auto p = std::make_shared<JPPart>();
        p->id = id;
        config.addPart(p);
    }
    int restored = 0;
    JPLibraryHistory h(config, [&restored] { ++restored; });
    h.start();
    assert(!h.canUndo() && !h.note());

    // A part's value changed: one step, the part still the same object after undo and redo.
    JPPart* r1 = config.part("R1");
    r1->value = "10k";
    assert(h.note() && h.undoText() == "Change Part R1" && h.serial() == 1);
    h.undo();
    assert(config.part("R1") == r1 && r1->value.empty() && restored == 1);
    h.redo();
    assert(config.part("R1") == r1 && r1->value == "10k" && restored == 2);

    // A part deleted, then three; undone, all back, in their places.
    config.removePart("C1");
    assert(h.note() && h.undoText() == "Delete Part C1");
    config.removePart("R1");
    config.removePart("R2");
    config.removePart("R3");
    assert(h.note() && h.undoText() == "Delete 3 Parts" && config.parts().empty());
    h.undo();
    assert(config.parts().size() == 3 && config.part("R2") && config.parts()[1]->id == "R2");
    h.undo();
    assert(config.part("C1") && config.parts().size() == 4);
    h.redo();
    assert(!config.part("C1") && h.redoText() == "Delete 3 Parts");

    // A new package and footprint; a change after an undo drops what could be redone.
    auto k = std::make_shared<JPPackage>();
    k->id = "SOT-23";
    config.addPackage(k);
    assert(h.note() && h.undoText() == "New Package SOT-23" && !h.canRedo());
    auto f = std::make_shared<JPLibraryFootprint>();
    f->name = "SOT-23-3";
    f->packageId = "SOT-23";
    config.addFootprint(f);
    k->footprint.bodyWidth = 1.3;
    assert(h.note() && h.undoText() == "Change Package SOT-23, New Footprint SOT-23-3");
    h.undo();
    assert(config.footprints().empty() && config.package("SOT-23")->footprint.bodyWidth == 0);
    h.undo();
    assert(!config.package("SOT-23"));
    // Manufacturers.
    config.manufacturers().push_back({ "Texas Instruments", { "TI" } });
    assert(h.note() && h.undoText() == "Change Manufacturers");
    fs::remove_all(dir);
    return 0;
}
