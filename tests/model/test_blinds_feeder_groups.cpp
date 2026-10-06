// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's BlindsFeederTest: blinds feeders on one holder (fiducial 1 the same) connected, one fixed by its fiducial
// 1 alone taking the holder's other fiducials, one elsewhere not connected; lanes numbered by their pocket
// centerline; the whole holder moved by fiducial 1; groups: joined by name, renamed together, not joined by a feeder
// at the same place in another group, a lone feeder joining a named group at its place, a named group of more than
// one not taking another group's name.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPBlindsFeeders.h"
#include "model/JPConfiguration.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

using namespace jf;
namespace fs = std::filesystem;

namespace {

constexpr const char* kBlinds = "org.openpnp.machine.reference.feeder.BlindsFeeder";
using B = JPBlindsFeeders;

struct Fiducials {
    JPLocation f1 { JPLengthUnit::Millimeters }, f2 { JPLengthUnit::Millimeters }, f3 { JPLengthUnit::Millimeters };
    Fiducials() = default;
    Fiducials(double x1, double y1, double x2, double y2, double x3, double y3)
        : f1(JPLengthUnit::Millimeters, x1, y1, 0, 0), f2(JPLengthUnit::Millimeters, x2, y2, 0, 0), f3(JPLengthUnit::Millimeters, x3, y3, 0, 0) {}
};

JPLocation fiducial(const JPFeeder& f, int n) {
    return f.locationOf("fiducial-" + std::to_string(n) + "-location").convertToUnits(JPLengthUnit::Millimeters);
}

// OpenPnP's BlindsFeederTestCondition: what one feeder should be.
struct Condition {
    std::string           id;
    int                   number;   // the test's feeder number, for its messages
    Fiducials             fiducials;
    std::string           group = B::kDefaultGroup;
    int                   feederNo = -1;   // -1: not checked
    std::set<std::string> connected;       // the others connected to it
};

struct Bench {
    fs::path        dir;
    JPConfiguration config;
    std::vector<Condition*> conditions;

    Bench() : dir(fs::temp_directory_path() / ("jplacer-blinds-" + std::to_string(::getpid()))), config(dir.string()) {}
    ~Bench() { fs::remove_all(dir); }
    std::string add() {
        JPFeeder f = JPFeeder::create(kBlinds, "");
        const std::string id = f.id();
        config.addFeeder(std::move(f));
        return id;
    }
    void setFiducials(const std::string& id, const Fiducials& fi) {
        B::setFiducial(config, id, 1, fi.f1);
        B::setFiducial(config, id, 2, fi.f2);
        B::setFiducial(config, id, 3, fi.f3);
    }
    // OpenPnP's testAllBlindsFeederConditions.
    void check() {
        for (const Condition* c : conditions) {
            const JPFeeder& f = *config.feeder(c->id);
            auto fail = [&](const char* what) {
                std::fprintf(stderr, "FeederID:%d : %s\n", c->number, what);
                return false;
            };
            assert(fiducial(f, 1) == c->fiducials.f1 || fail("Fiducial 1 incorrect"));
            assert(fiducial(f, 2) == c->fiducials.f2 || fail("Fiducial 2 incorrect"));
            assert(fiducial(f, 3) == c->fiducials.f3 || fail("Fiducial 3 incorrect"));
            assert(B::groupName(f) == c->group || fail("Group name incorrect"));
            // OpenPnP's getConnectedFeeders: those with its fiducial 1, itself among them.
            const std::vector<std::string> connected = B::connected(config, c->id, fiducial(f, 1), true);
            assert(connected.size() == c->connected.size() + 1 || fail("connected count"));
            for (const std::string& other : c->connected)
                assert(std::count(connected.begin(), connected.end(), other) == 1 || fail("Incorrect feeders connected"));
            if (c->feederNo >= 0) assert(f.number("feeder-no") == c->feederNo || fail("Feeder number"));
        }
    }
};

// OpenPnP's BlindsFeederTestConditionGroup: each condition in it connected to all the others.
struct Group {
    std::vector<Condition*> members;
    void relink() {
        for (Condition* c : members) {
            c->connected.clear();
            for (Condition* o : members)
                if (o != c) c->connected.insert(o->id);
        }
    }
    void add(Condition& c) {
        for (Condition* m : members) m->connected.clear();
        if (std::find(members.begin(), members.end(), &c) == members.end()) members.push_back(&c);
        relink();
    }
    void remove(Condition& c) {
        for (Condition* m : members) m->connected.clear();
        members.erase(std::remove(members.begin(), members.end(), &c), members.end());
        relink();
    }
};

void testBlindsFeederBasics() {
    Bench b;
    const Fiducials fid1(180, 100, 100, 100, 100, 165), fid2(280, 100, 200, 100, 200, 165), fid2Partial(280, 100, 0, 0, 0, 0),
        fid3(380, 100, 300, 100, 300, 165);
    // A new feeder, its fiducials set.
    Condition c1 { b.add(), 1, fid1 };
    b.conditions.push_back(&c1);
    Group g1;
    g1.add(c1);
    b.setFiducials(c1.id, fid1);
    b.check();
    // Another at the same place, by fiducial 1 alone: connected, the others taken from the holder.
    Condition c2 { b.add(), 1, fid1 };
    b.conditions.push_back(&c2);
    g1.add(c2);
    B::setFiducial(b.config, c2.id, 1, fid1.f1);
    b.check();
    // One elsewhere: not connected.
    Condition c3 { b.add(), 1, fid2Partial };
    b.conditions.push_back(&c3);
    B::setFiducial(b.config, c3.id, 1, fid2.f1);
    b.check();
    c3.fiducials = fid2;
    B::setFiducial(b.config, c3.id, 2, fid2.f2);
    B::setFiducial(b.config, c3.id, 3, fid2.f3);
    b.check();
    // The pockets: lanes numbered by centerline.
    auto pockets = [&b](const std::string& id, double centerline) {
        B::setPocketCenterline(b.config, id, centerline);
        b.config.feeder(id)->setLengthOf("pocket-pitch", JPLength(4.0, JPLengthUnit::Millimeters));
        b.config.feeder(id)->setLengthOf("pocket-size", JPLength(2.8, JPLengthUnit::Millimeters));
    };
    c1.feederNo = 2;
    c2.feederNo = 1;
    pockets(c1.id, 9.0);
    b.check();
    c1.feederNo = 1;
    c2.feederNo = 2;
    pockets(c2.id, 19.0);
    b.check();
    // Fiducial 1 moved: the connected feeders' fiducials move with it.
    c1.fiducials = fid3;
    c2.fiducials = fid3;
    B::setFiducial(b.config, c1.id, 1, fid3.f1);
    b.check();
}

void testBlindsFeederGroups() {
    Bench b;
    const std::string group1 = "BlindsFeederTestGroup1", group2 = "BlindsFeederTestGroup2";
    const Fiducials fid1(180, 100, 100, 100, 100, 165), fid2(280, 100, 200, 100, 200, 165), fid3(380, 100, 300, 100, 300, 165);
    Group g1;
    Condition c1 { b.add(), 1, fid1 };
    b.conditions.push_back(&c1);
    g1.add(c1);
    b.setFiducials(c1.id, fid1);
    b.check();
    // A second connected by fiducial 1, in its group; renaming the group renames it on both.
    Condition c2 { b.add(), 2, fid1 };
    b.conditions.push_back(&c2);
    g1.add(c2);
    B::setFiducial(b.config, c2.id, 1, fid1.f1);
    b.check();
    c1.group = c2.group = group1;
    B::setGroupName(b.config, c1.id, group1);
    b.check();
    // A third at the same place: in its own group, not the named one.
    Condition c3 { b.add(), 3 };
    b.conditions.push_back(&c3);
    c3.fiducials.f1 = fid1.f1;
    B::setFiducial(b.config, c3.id, 1, fid1.f1);
    b.check();
    c3.fiducials = fid1;
    B::setFiducial(b.config, c3.id, 2, fid1.f2);
    B::setFiducial(b.config, c3.id, 3, fid1.f3);
    b.check();
    // Moved elsewhere; the others stay.
    c3.fiducials = fid2;
    b.setFiducials(c3.id, fid2);
    b.check();
    // A new feeder joining the named group: it takes its place.
    Condition c4 { b.add(), 4, fid1, group1 };
    b.conditions.push_back(&c4);
    g1.add(c4);
    B::setGroupName(b.config, c4.id, group1);
    b.check();
    // A lone placed feeder joining the named group.
    Condition c5 { b.add(), 5, fid3 };
    b.conditions.push_back(&c5);
    b.setFiducials(c5.id, fid3);
    b.check();
    g1.add(c5);
    c5.fiducials = fid1;
    c5.group = group1;
    B::setGroupName(b.config, c5.id, group1);
    b.check();
    // A lone placed feeder of a name of its own joining the named group.
    Condition c6 { b.add(), 6, fid3, "BlindsFeederTemporaryTestGroup" };
    b.conditions.push_back(&c6);
    b.setFiducials(c6.id, fid3);
    B::setGroupName(b.config, c6.id, "BlindsFeederTemporaryTestGroup");
    b.check();
    assert(B::groupName(*b.config.feeder(c6.id)) == "BlindsFeederTemporaryTestGroup");
    assert(B::connected(b.config, c6.id, fiducial(*b.config.feeder(c6.id), 1), true).size() == 1);
    g1.add(c6);
    c6.fiducials = fid1;
    c6.group = group1;
    B::setGroupName(b.config, c6.id, group1);
    b.check();
    // Back to Default out of a named group of more than one.
    g1.remove(c6);
    c6.group = "Default";
    B::setGroupName(b.config, c6.id, "Default");
    b.check();
    // Removed: fiducial 1's place is free for what follows.
    b.config.removeFeeder(c6.id);
    b.conditions.erase(std::remove(b.conditions.begin(), b.conditions.end(), &c6), b.conditions.end());
    b.check();
    // A named group of two does not join another group at its place...
    Group g2;
    Condition c7 { b.add(), 7, fid1 };
    b.conditions.push_back(&c7);
    g2.add(c7);
    Condition c8 { b.add(), 8, fid1 };
    b.conditions.push_back(&c8);
    b.setFiducials(c7.id, fid1);
    g2.add(c8);
    c7.group = c8.group = group2;
    B::setGroupName(b.config, c7.id, group2);
    B::setGroupName(b.config, c8.id, group2);
    b.check();
    B::setGroupName(b.config, c7.id, group1);   // refused
    b.check();
    // ...nor elsewhere.
    c7.fiducials = c8.fiducials = fid3;
    b.setFiducials(c7.id, fid3);
    B::setGroupName(b.config, c7.id, group1);
    b.check();
}

} // namespace

int main() {
    testBlindsFeederBasics();
    testBlindsFeederGroups();
    return 0;
}
