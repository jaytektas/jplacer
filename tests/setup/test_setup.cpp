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

// The bundled grblHAL profile, as a controller can name it.
std::vector<JPFirmwareProfile> grblhal() {
    JPFirmwareProfile p;
    std::string error;
    const bool ok = p.load(std::string(JPLACER_PROFILES_DIR) + "/grblhal.json", error);
    assert(ok);
    return { p };
}

int main() {
    JPCellConfig c = cell();

    // The tree: the machine's parts in their groups, a head's parts under it.
    const JPSetupTree::Node root = JPSetupTree::build(c);
    assert(root.label == "Bench" && root.path == "machine");
    assert(find(root, "group:axes")->children.size() == 3);
    assert(find(root, "group:nozzles:H")->children.front().path == "nozzle:N");
    // Each named as OpenPnP's tree names it: its class, then its name.
    const std::string bottom = find(root, "group:cameras:")->children.front().label;
    assert(bottom.size() > 7 && bottom.compare(bottom.size() - 7, 7, " BOTTOM") == 0);
    const auto labels = JPSetupTree::labelsTo(root, "nozzle:N");
    assert(labels.size() == 5 && labels[2] == "ReferenceHead H1" && labels[4] == "ReferenceNozzle LEFT");
    assert(root.children.front().path == "group:axes" && root.children.back().path == "group:vision");
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
            const JPSetupProperties::Form f = JPSetupProperties::forNode(c, path, grblhal());
            for (const JProperty& p : f.model.all()) p.get();
        }
        assert(c.toJson().dump() == before);
    }

    // The machine's discard location: set, it has coordinates to fill in.
    {
        JPSetupProperties::Form m = JPSetupProperties::forNode(c, "machine", {});
        assert(!m.model.find("discardX") && m.model.set("discard", JVariant(true)) && c.discardLocation);
        m = JPSetupProperties::forNode(c, "machine", {});
        assert(m.model.set("discardX", JVariant(40.0)) && c.discardLocation->x == 40.0);
        c.discardLocation.reset();
    }

    // A controller's commands: the profile's unless replaced; emptied, the profile's again.
    {
        JPSetupProperties::Form d = JPSetupProperties::forNode(c, "driver:D", grblhal());
        const JProperty* home = d.model.find("command:home");
        assert(home && home->get().toString().empty() && home->meta.def.find("grblhal: ") == 0);
        assert(d.model.set("command:home", JVariant(std::string("G28"))) && c.drivers[0].commands.at("home") == "G28");
        assert(d.model.set("command:home", JVariant(std::string())) && c.drivers[0].commands.empty());
        // The serial settings, as OpenPnP has them.
        assert(d.model.set("parity", JVariant(std::string("even"))) && c.drivers[0].link["parity"].str() == "even");
        assert(d.model.set("dataBits", JVariant(std::string("7"))) && c.drivers[0].link["dataBits"].number() == 7);
        c.drivers[0].link["parity"] = "none";
        c.drivers[0].link["dataBits"] = 8;
    }

    // Settings: by name, written into the cell; a reshaping change.
    JPSetupProperties::Form ax = JPSetupProperties::forNode(c, "axis:Z", grblhal());
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
    // Its Format, as OpenPnP's: one of the modes the device offers (its size, rate and kind together).
    JPSetupProperties::Live live;
    live.cameraModes = [](const std::string&) {
        return std::vector<JPCaptureMode> { { "MJPG", 1280, 720, 30 }, { "MJPG", 1280, 720, 15 }, { "YUYV", 640, 480, 30 } };
    };
    cam = JPSetupProperties::forNode(c, "camera:C", {}, nullptr, nullptr, nullptr, live);
    assert(cam.model.set("format", JVariant(std::string("1280 x 720, 15 FPS, MJPG"))));
    assert(c.cameras[0].device["format"].str() == "MJPG" && c.cameras[0].device["width"].number() == 1280
           && c.cameras[0].device["height"].number() == 720 && c.cameras[0].device["fps"].number() == 15);
    assert(cam.model.get("format").toString() == "1280 x 720, 15 FPS, MJPG");
    assert(cam.model.set("format", JVariant(std::string("The largest MJPG, at its fastest"))));
    assert(c.cameras[0].device["width"].number() == 0 && c.cameras[0].device["format"].str().empty());
    // Set without a rate (older settings): shown as the listed format it opens in, not as "0 FPS".
    c.cameras[0].device["format"] = "MJPG";
    c.cameras[0].device["width"] = 1280;
    c.cameras[0].device["height"] = 720;
    cam = JPSetupProperties::forNode(c, "camera:C", {}, nullptr, nullptr, nullptr, live);
    assert(cam.model.get("format").toString() == "1280 x 720, 30 FPS, MJPG");
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
    const std::string tipId = tip.substr(10);
    assert(nozzle.model.get("loaded:T").toBool() && nozzle.model.get("fits:T").toBool());
    assert(!nozzle.model.get("fits:" + tipId).toBool());
    // Laid out as OpenPnP lays out a nozzle: its tips on a tab of their own,
    // the axes and offsets as columns under X / Y / Z / Rotation.
    assert(nozzle.tabs.size() == 6 && nozzle.tabs[0].title == "Configuration" && nozzle.tabs[1].title == "Nozzle Tips"
           && nozzle.tabs[2].title == "Vacuum" && nozzle.tabs[3].title == "Tool Changer" && nozzle.tabs[4].title == "Homing" && nozzle.tabs[5].title == "Offset Wizard");
    // A ContactProbeNozzle has OpenPnP's Contact Probe tab too.
    {
        JPCellConfig probing = c;
        probing.nozzles[0].contactProbe.nozzle = true;
        const JPSetupProperties::Form f = JPSetupProperties::forNode(probing, "nozzle:N", {});
        assert(f.tabs.size() == 7 && f.tabs[6].title == "Contact Probe");
    }
    {
        const JPSetupProperties::Group& cs = nozzle.tabs[0].groups[1];
        assert(cs.title == "Coordinate System" && cs.rows[1].kind == JPSetupProperties::Row::Kind::Header);
        assert(cs.rows[3].label == "Offset" && cs.rows[3].cells.size() == 3 && cs.rows[3].cells[0].property == "offsetX");
    }
    JPSetupProperties::Form newTip = JPSetupProperties::forNode(c, tip, {});
    assert(newTip.model.set("fits:N", JVariant(true)) && c.nozzles[0].tipIds.size() == 2);
    nozzle = JPSetupProperties::forNode(c, "nozzle:N", {});
    assert(nozzle.model.set("loaded:" + tipId, JVariant(true)) && c.nozzles[0].tipId == tipId);
    assert(!nozzle.model.get("loaded:T").toBool());   // one tip on a nozzle
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
    // Controllers: Add asks which kind (OpenPnP's driver classes), each named after it.
    {
        JPCellConfig c = cell();
        assert(JPSetupEdits::kinds(c, "group:drivers").size() == 4);
        assert(JPSetupEdits::add(c, "group:drivers").empty());
        const size_t before = c.drivers.size();
        assert(!JPSetupEdits::add(c, "group:drivers", "NullDriver").empty());
        assert(!JPSetupEdits::add(c, "group:drivers", "NeoDen4Driver").empty());
        assert(!JPSetupEdits::add(c, "group:drivers", "GcodeAsyncDriver").empty());
        assert(c.drivers.size() == before + 3);
        assert(c.drivers[before].name == "NullDriver" && c.drivers[before].link["type"].str() == "simulated");
        assert(c.drivers[before + 1].link["type"].str() == "neoden4" && c.drivers[before + 1].profile == "neoden4");
        assert(c.drivers[before + 2].link["type"].str() == "serial" && c.drivers[before + 2].profile == "auto");
    }
    // Signalers: Add asks which kind (OpenPnP's New Signaler…), each kind its own page.
    {
        JPCellConfig c = cell();
        assert(JPSetupEdits::addable(c, "group:signalers") == "Signaler");
        assert(JPSetupEdits::kinds(c, "group:signalers").size() == 3);
        assert(JPSetupEdits::kinds(c, "group:heads").empty());
        // An axis is one of OpenPnP's axis classes, chosen as its New Axis… asks.
        assert(JPSetupEdits::kinds(c, "group:axes").size() == 6);
        const std::string cam = JPSetupEdits::add(c, "group:axes", "ReferenceCamClockwiseAxis");
        assert(c.axes.back().kind == JPAxisConfig::Kind::Cam && c.axes.back().camClockwise && cam.rfind("axis:AXS", 0) == 0);
        assert(JPSetupEdits::add(c, "group:signalers").empty());   // no kind: nothing added
        const std::string sound = JPSetupEdits::add(c, "group:signalers", "SoundSignaler");
        const std::string act = JPSetupEdits::add(c, "group:signalers", "ActuatorSignaler");
        assert(c.signalers.size() == 2 && sound.rfind("signaler:SIG", 0) == 0 && c.signalers[0].name == "SoundSignaler");
        assert(JPSetupTree::groupOf(c, act) == "group:signalers");
        assert(!JPSetupTree::labelsTo(JPSetupTree::build(c), act).empty());
        JPSetupProperties::Form f = JPSetupProperties::forNode(c, sound, {});
        assert(f.title == "SoundSignaler SoundSignaler");
        assert(f.model.find("errorSound") && f.model.find("finishedSound"));
        f.model.set("errorSound", JVariant(true));
        assert(c.signalers[0].errorSound);
        f = JPSetupProperties::forNode(c, act, {});
        f.model.set("actuator", JVariant(std::string("LIGHT_UP")));
        f.model.set("jobState", JVariant(std::string("FINISHED")));
        assert(c.signalers[1].actuatorId == "L" && c.signalers[1].jobState == JPSignalerConfig::JobState::Finished);
        f.model.set("jobState", JVariant(std::string("")));
        assert(!c.signalers[1].jobState);
        // Its actuator is in use while it switches it.
        std::string why;
        c.cameras.clear();
        assert(!JPSetupEdits::remove(c, "actuator:L", why) && why.find("signaler") != std::string::npos);
        assert(JPSetupEdits::move(c, act, -1) == act && c.signalers[0].kind == JPSignalerConfig::Kind::Actuator);
        assert(JPSetupEdits::remove(c, act, why) && c.signalers.size() == 1);
        assert(JPSetupEdits::remove(c, "actuator:L", why));
        c.signalers.push_back({ JPSignalerConfig::Kind::Actuator, "S9", "Beacon", false, false, "gone", {} });
        bool said = false;
        for (const std::string& p : c.problems()) said |= p.find("signaler Beacon") != std::string::npos;
        assert(said);
    }
    std::printf("  [OK] signalers\n");
    std::printf("All setup tests passed.\n");
    return 0;
}
