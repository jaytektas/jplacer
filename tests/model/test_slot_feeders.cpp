// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Slot feeders and their banks: the banks taken from a machine.xml's
// properties; a slot's load found from its bank and feeder ids (the later
// slot keeping a feeder named by two); its name, part, enabled and pick
// location from what is loaded; loading, choosing a bank, the only bank kept;
// and the banks written and read back as they were.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace jf;
namespace fs = std::filesystem;

namespace {

constexpr const char* kMachine = R"(<openpnp-machine>
<machine class="org.openpnp.machine.reference.ReferenceMachine" id="M">
<feeders>
<feeder class="org.openpnp.machine.reference.feeder.ReferenceSlotAutoFeeder" id="SLOT-1" name="Slot 1" enabled="true" part-id="" bank-id="BANK-A" feeder-id="SLOTFDR-1" actuator-name="Feed" actuator-value="1.0">
<location units="Millimeters" x="100.0" y="50.0" z="-2.0" rotation="90.0"/>
</feeder>
<feeder class="org.openpnp.machine.reference.feeder.ReferenceSlotAutoFeeder" id="SLOT-2" name="Slot 2" enabled="true" part-id="" bank-id="BANK-A" feeder-id="SLOTFDR-2">
<location units="Millimeters" x="120.0" y="50.0" z="-2.0" rotation="0.0"/>
</feeder>
<feeder class="org.openpnp.machine.reference.feeder.ReferenceSlotAutoFeeder" id="SLOT-3" name="Slot 3" enabled="true" part-id="" bank-id="BANK-A" feeder-id="SLOTFDR-2">
<location units="Millimeters" x="140.0" y="50.0" z="-2.0" rotation="0.0"/>
</feeder>
</feeders>
<properties>
<entry>
<string>ReferenceAutoFeederSlot.banks</string>
<object class="org.openpnp.machine.reference.feeder.ReferenceSlotAutoFeeder$BanksProperty">
<banks>
<bank id="BANK-A" name="Front">
<feeders>
<feeder id="SLOTFDR-1" name="F1" part-id="R1">
<offsets units="Millimeters" x="1.0" y="2.0" z="0.0" rotation="0.0"/>
</feeder>
<feeder id="SLOTFDR-2" name="F2">
<offsets units="Millimeters" x="0.0" y="0.0" z="0.0" rotation="0.0"/>
</feeder>
</feeders>
</bank>
</banks>
</object>
</entry>
</properties>
</machine>
</openpnp-machine>
)";

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-slot-feeders";
    fs::remove_all(dir);
    fs::create_directories(dir);
    { std::ofstream(dir / "machine.xml") << kMachine; }

    JPConfiguration config(dir.string());
    std::string error;
    assert(config.importFeeders((dir / "machine.xml").string(), error) == 3);

    // Slot 1: F1 loaded, holding R1: its name, part and enabled; its pick is
    // the offsets turned by the slot's rotation, from the slot.
    JPFeeder* s1 = config.feeder("SLOT-1");
    assert(s1->isSlot() && s1->feedsAs() == "ReferenceAutoFeeder");
    assert(s1->name() == "Slot 1 (F1)" && s1->partId() == "R1" && s1->enabled());
    const auto at = s1->pickLocation();
    assert(at && near(at->x(), 100 - 2) && near(at->y(), 50 + 1) && near(at->rotation(), 90));

    // F2 named by slots 2 and 3: the later keeps it; 2 is empty, so not enabled.
    JPFeeder* s2 = config.feeder("SLOT-2");
    JPFeeder* s3 = config.feeder("SLOT-3");
    assert(s2->name() == "Slot 2 (None)" && !s2->slotLoad && !s2->enabled());
    assert(s3->name() == "Slot 3 (F2)");
    // F2 holds no part: not enabled.
    assert(!s3->enabled());
    std::string why;
    assert(!s2->feed(why) && why == "No feeder loaded in slot.");

    // A name set as shown has what is loaded taken off.
    s1->setName("Left (F1)");
    assert(s1->text("name") == "Left" && s1->name() == "Left (F1)");

    // Loading F1 into slot 2 takes it out of slot 1.
    config.loadSlot("SLOT-2", "SLOTFDR-1");
    assert(config.feeder("SLOT-2")->name() == "Slot 2 (F1)" && config.feeder("SLOT-1")->name() == "Left (None)");

    // A new bank: the slot moves to it, empty; the only bank cannot go, another can.
    JPSlotBanks& banks = config.slotBanks("ReferenceSlotAutoFeeder");
    const std::string back = banks.addBank();
    config.setSlotBank("SLOT-2", back);
    assert(!config.feeder("SLOT-2")->slotLoad && config.slotBankId(*config.feeder("SLOT-2")) == back);
    assert(config.slotBanks("ReferenceSlotAutoFeeder").removeBank(back, why));
    config.resolveSlots();
    // Its bank gone: in the last bank.
    assert(config.slotBankId(*config.feeder("SLOT-2")) == "BANK-A");
    assert(!config.slotBanks("ReferenceSlotAutoFeeder").removeBank("BANK-A", why));
    assert(why == "Can't delete the only bank. There must always be one bank defined.");

    // A Schultz slot's banks: made with one, "Default".
    const auto schultzBanks = config.slotBanks("SlotSchultzFeeder").banks();
    assert(schultzBanks.size() == 1 && schultzBanks[0].name == "Default");

    // Written and read back.
    config.loadSlot("SLOT-3", "SLOTFDR-1");
    config.slotBanks("ReferenceSlotAutoFeeder").setFeederPart("BANK-A", "SLOTFDR-1", "C7");
    config.resolveSlots();
    assert(config.save(error));
    JPConfiguration again(dir.string());
    std::vector<std::string> problems;
    assert(again.load(problems, error));
    assert(again.feeder("SLOT-3")->name() == "Slot 3 (F1)" && again.feeder("SLOT-3")->partId() == "C7");
    assert(again.slotBanks("ReferenceSlotAutoFeeder").banks()[0].name == "Front");
    fs::remove_all(dir);
    return 0;
}
