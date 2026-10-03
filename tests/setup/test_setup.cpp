// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Machine Setup's model of a cell: the tree, adding, removing (refused while
// a part is in use) and reordering parts, and the settings each part edits.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPSetupEdits.h"
#include "setup/JPSetupProperties.h"
#include "setup/JPSetupTree.h"

#include <cstdio>
#include <functional>

using namespace jf;

namespace {

JPCellConfig cell() {
    JPCellConfig c;
    c.name = "Bench";
    JPDriverConfig d;
    d.id = "D";
    d.name = "Jaytek";
    d.link = JJson::object();
    d.link["type"] = "serial";
    c.drivers.push_back(d);
    for (const char* id : { "X", "Y", "Z" }) {
        JPAxisConfig a;
        a.id = id;
        a.name = id;
        a.driverId = "D";
        c.axes.push_back(a);
    }
    JPHeadConfig h;
    h.id = "H";
    h.name = "H1";
    c.heads.push_back(h);
    JPNozzleConfig n;
    n.id = "N";
    n.name = "LEFT";
    n.mount.headId = "H";
    n.mount.axisX = "X";
    n.mount.axisY = "Y";
    n.tipIds = { "T" };
    n.tipId = "T";
    c.nozzles.push_back(n);
    c.nozzleTips.push_back({ "T", "503", 0.75 });
    c.nozzleTips.push_back({ "T2", "504", 1.1 });
    JPActuatorConfig light;
    light.id = "L";
    light.name = "LIGHT_UP";
    c.actuators.push_back(light);
    JPCameraConfig cam;
    cam.id = "C";
    cam.name = "BOTTOM";
    cam.looksUp = true;
    cam.device = JJson::object();
    cam.device["light-actuator-id"] = "L";
    c.cameras.push_back(cam);
    return c;
}

const JPSetupTree::Node* find(const JPSetupTree::Node& n, const std::string& path) {
    if (n.path == path) return &n;
    for (const JPSetupTree::Node& c : n.children)
        if (const JPSetupTree::Node* f = find(c, path)) return f;
    return nullptr;
}

} // namespace

int main() {
    JPCellConfig c = cell();

    // The tree: the machine's parts in their groups, a head's parts under it.
    const JPSetupTree::Node root = JPSetupTree::build(c);
    assert(root.label == "Bench" && root.path == "machine");
    assert(find(root, "group:axes")->children.size() == 3);
    assert(find(root, "group:nozzles:H")->children.front().path == "nozzle:N");
    assert(find(root, "group:cameras:")->children.front().label == "BOTTOM");
    const auto labels = JPSetupTree::labelsTo(root, "nozzle:N");
    assert(labels.size() == 5 && labels[2] == "H1" && labels[4] == "LEFT");
    assert(JPSetupTree::groupOf(c, "nozzle:N") == "group:nozzles:H");
    std::printf("  [OK] the tree\n");

    // Adding goes where the selection is; removing is refused while in use.
    assert(JPSetupEdits::addable(c, "camera:C") == "Camera" && JPSetupEdits::addable(c, "machine").empty());
    const std::string added = JPSetupEdits::add(c, "group:nozzles:H");
    assert(added.rfind("nozzle:NOZ", 0) == 0 && c.nozzles.back().mount.headId == "H");
    std::string why;
    assert(!JPSetupEdits::remove(c, "axis:X", why) && why == "nozzle LEFT uses it");
    assert(!JPSetupEdits::remove(c, "actuator:L", why) && why.find("its light") != std::string::npos);
    assert(!JPSetupEdits::remove(c, "head:H", why) && why.find(" and ") != std::string::npos);
    assert(JPSetupEdits::remove(c, added, why) && c.nozzles.size() == 1);
    // A nozzle tip on a nozzle stays; one only fitting it goes from its list.
    assert(JPSetupEdits::addable(c, "nozzletip:T") == "Nozzle Tip");
    assert(!JPSetupEdits::remove(c, "nozzletip:T", why) && why == "nozzle LEFT (it is on it) uses it");
    c.nozzles[0].tipIds.push_back("T2");
    assert(JPSetupEdits::remove(c, "nozzletip:T2", why) && c.nozzles[0].tipIds.size() == 1);
    assert(c.problems().empty());
    assert(JPSetupEdits::move(c, "axis:Y", -1) == "axis:Y" && c.axes[0].id == "Y");
    assert(JPSetupEdits::move(c, "axis:Y", -1).empty());
    std::printf("  [OK] add, remove, move\n");

    // Reading every setting of every part changes nothing.
    {
        const std::string before = c.toJson().dump();
        std::vector<std::string> paths;
        std::function<void(const JPSetupTree::Node&)> walk = [&](const JPSetupTree::Node& n) {
            paths.push_back(n.path);
            for (const JPSetupTree::Node& k : n.children) walk(k);
        };
        walk(JPSetupTree::build(c));
        for (const std::string& path : paths) {
            const JPSetupProperties::Form f = JPSetupProperties::forNode(c, path, { "grblhal" });
            for (const JProperty& p : f.model.all()) p.get();
        }
        assert(c.toJson().dump() == before);
    }

    // Settings: by name, written into the cell; a reshaping change.
    JPSetupProperties::Form ax = JPSetupProperties::forNode(c, "axis:Z", { "grblhal" });
    assert(ax.title == "Axis Z" && ax.model.find("driver") && !ax.model.find("inputAxis"));
    assert(ax.model.get("driver").toString() == "Jaytek");
    assert(ax.model.set("kind", JVariant(std::string("mapped"))) && c.axes[2].kind == JPAxisConfig::Kind::Mapped);
    ax = JPSetupProperties::forNode(c, "axis:Z", {});
    assert(ax.model.find("inputAxis") && !ax.model.find("driver"));
    assert(ax.model.set("inputAxis", JVariant(std::string("X"))) && c.axes[2].inputAxisId == "X");

    // A fixed camera put on the head moves with it, on the head's X and Y.
    JPSetupProperties::Form cam = JPSetupProperties::forNode(c, "camera:C", {});
    assert(cam.model.get("light").toString() == "LIGHT_UP" && !cam.model.find("axisX"));
    assert(cam.model.set("head", JVariant(std::string("H1"))));
    assert(c.cameras[0].mount.headId == "H" && c.cameras[0].mount.axisX == "X" && c.cameras[0].mount.axisY == "Y");
    cam.model.set("width", JVariant(1280));
    assert(c.cameras[0].device["width"].number() == 1280);
    // How much of a straightened picture's edge shows: a percentage here, a share in the cell.
    cam.model.set("showAll", JVariant(40));
    assert(c.cameras[0].showAll == 0.4 && JPCameraConfig::fromJson(c.cameras[0].toJson()).showAll == 0.4);

    JPSetupProperties::Form head = JPSetupProperties::forNode(c, "head:H", {});
    assert(!head.model.find("homingFiducialX"));
    head.model.set("homingFiducial", JVariant(true));
    assert(c.heads[0].homingFiducial.has_value());
    assert(JPSetupProperties::forNode(c, "group:axes", {}).model.empty());

    // A tip fits the nozzles ticked on it; the nozzle has one of those on it.
    const std::string tip = JPSetupEdits::add(c, "group:nozzletips");
    JPSetupProperties::Form nozzle = JPSetupProperties::forNode(c, "nozzle:N", {});
    assert(nozzle.model.get("tip").toString() == "503" && nozzle.model.find("tip")->meta.choices.size() == 2);
    JPSetupProperties::Form newTip = JPSetupProperties::forNode(c, tip, {});
    assert(newTip.model.set("fits:N", JVariant(true)) && c.nozzles[0].tipIds.size() == 2);
    nozzle = JPSetupProperties::forNode(c, "nozzle:N", {});
    assert(nozzle.model.set("tip", JVariant(std::string("New nozzle tip"))) && c.nozzles[0].tipId == tip.substr(10));
    newTip.model.set("fits:N", JVariant(false));
    assert(c.nozzles[0].tipIds.size() == 1 && c.nozzles[0].tipId.empty());
    assert(c.problems().empty());
    std::printf("  [OK] settings\n");

    // A tip's changer: steps added after the one selected, moved, set; unloading backwards shown, not changed.
    {
        const JPSetupTree::Node tree = JPSetupTree::build(c);
        assert(find(tree, "group:load:T") && find(tree, "group:unload:T")->label == "Unload (loading backwards)");
        assert(JPSetupEdits::addable(c, "group:load:T") == "Step" && JPSetupEdits::addable(c, "group:unload:T").empty());
        assert(JPSetupEdits::add(c, "group:load:T") == "step:T:load:0");
        assert(c.problems().size() == 1);   // a first move must say where
        JPSetupProperties::Form s = JPSetupProperties::forNode(c, "step:T:load:0", {});
        assert(s.model.set("x", JVariant(std::string("-16"))) && s.model.set("y", JVariant(std::string("130"))));
        assert(s.model.set("z", JVariant(std::string("0"))) && c.problems().empty());
        s.model.set("z", JVariant(std::string("nonsense")));
        assert(c.nozzleTips[0].loadSteps[0].z == 0.0);   // not a number: not taken
        assert(JPSetupEdits::add(c, "step:T:load:0") == "step:T:load:1");
        s = JPSetupProperties::forNode(c, "step:T:load:1", {});
        s.model.set("z", JVariant(std::string("-27")));
        s.model.set("speed", JVariant(50));
        assert(JPSetupTree::stepLabel(c, c.nozzleTips[0].loadSteps[1], 1) == "2. Move to Z -27 at 50%");
        assert(JPSetupEdits::add(c, "step:T:load:0") == "step:T:load:1");   // between the two
        s = JPSetupProperties::forNode(c, "step:T:load:1", {});
        s.model.set("kind", JVariant(std::string("wait")));
        assert(JPSetupEdits::move(c, "step:T:load:1", +1) == "step:T:load:2");
        assert(c.nozzleTips[0].loadSteps[2].kind == JPChangerStep::Kind::Wait);
        std::string why;
        assert(!JPSetupEdits::remove(c, "step:T:unload:0", why));
        assert(JPSetupProperties::forNode(c, "step:T:unload:0", {}).model.get("what").toString() == "Move to X -16, Y 130, Z -27 at 50%");
        JPSetupProperties::Form tipForm = JPSetupProperties::forNode(c, "nozzletip:T", {});
        tipForm.model.set("unloading", JVariant(std::string("steps of its own")));
        assert(c.nozzleTips[0].unloadSteps.size() == 3 && JPSetupEdits::addable(c, "group:unload:T") == "Step");
        assert(JPSetupEdits::remove(c, "step:T:unload:2", why) && c.nozzleTips[0].unloadSteps.size() == 2);
    }
    std::printf("  [OK] a tip's changer steps\n");
    std::printf("All setup tests passed.\n");
    return 0;
}
