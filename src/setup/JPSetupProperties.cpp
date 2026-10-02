// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupProperties.h"

#include "JPSetupTree.h"

#include <functional>
#include <memory>
#include <optional>
#include <utility>

inline namespace jf {

namespace {

using Strings = std::vector<std::string>;

// Choices shown by name and kept by id: labels[i] names ids[i].
struct Named {
    Strings labels, ids;
    void add(std::string label, std::string id) {
        labels.push_back(std::move(label));
        ids.push_back(std::move(id));
    }
};

template <class T>
Named named(const std::vector<T>& items, const std::string& none) {
    Named n;
    if (!none.empty()) n.add(none, "");
    for (const T& i : items) n.add(i.name.empty() ? i.id : i.name, i.id);
    return n;
}

// Adds a category's properties to a model. Each reads and writes the cell
// through closures that find the part afresh each time (the cell's lists can
// move); the "ref" forms take a reference to a member of it.
class Adder {
public:
    explicit Adder(JPropertyModel& m) : m_model(m) {}

    void category(std::string c) { m_category = std::move(c); }

    void text(const std::string& name, const std::string& label, std::function<std::string()> get,
              std::function<void(const std::string&)> set) {
        JProperty p = make(name, label);
        p.get = [get] { return JVariant(get()); };
        if (set) p.set = [set](const JVariant& v) { set(v.toString()); return true; };   // none: shown, not edited
        m_model.add(std::move(p));
    }
    void text(const std::string& name, const std::string& label, std::function<std::string&()> ref) {
        text(name, label, [ref] { return ref(); }, [ref](const std::string& v) { ref() = v; });
    }
    void number(const std::string& name, const std::string& label, std::function<double()> get,
                std::function<void(double)> set, int decimals = 3) {
        JProperty p = make(name, label);
        p.meta.decimals = decimals;
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(v.toDouble()); return true; };
        m_model.add(std::move(p));
    }
    void number(const std::string& name, const std::string& label, std::function<double&()> ref, int decimals = 3) {
        number(name, label, [ref] { return ref(); }, [ref](double v) { ref() = v; }, decimals);
    }
    void integer(const std::string& name, const std::string& label, std::function<int()> get,
                 std::function<void(int)> set, int min, int max) {
        JProperty p = make(name, label);
        p.meta.min = JVariant(min);
        p.meta.max = JVariant(max);
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(int(v.toInt())); return true; };
        m_model.add(std::move(p));
    }
    void integer(const std::string& name, const std::string& label, std::function<int&()> ref, int min, int max) {
        integer(name, label, [ref] { return ref(); }, [ref](int v) { ref() = v; }, min, max);
    }
    void flag(const std::string& name, const std::string& label, std::function<bool()> get, std::function<void(bool)> set) {
        JProperty p = make(name, label);
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(v.toBool()); return true; };
        m_model.add(std::move(p));
    }
    void flag(const std::string& name, const std::string& label, std::function<bool&()> ref) {
        flag(name, label, [ref] { return ref(); }, [ref](bool v) { ref() = v; });
    }
    // One of `labels`, read and written as the label.
    void choice(const std::string& name, const std::string& label, const Strings& labels,
                std::function<std::string()> get, std::function<void(const std::string&)> set) {
        JProperty p = make(name, label);
        for (const std::string& l : labels) p.meta.choices.push_back(JVariant(l));
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(v.toString()); return true; };
        m_model.add(std::move(p));
    }
    // One of `n`, kept as its id.
    void byName(const std::string& name, const std::string& label, const Named& n, std::function<std::string()> get,
                std::function<void(const std::string&)> set) {
        choice(name, label, n.labels,
               [n, get] {
                   const std::string id = get();
                   for (size_t i = 0; i < n.ids.size(); ++i)
                       if (n.ids[i] == id) return n.labels[i];
                   return id;   // names something not in the cell: shown as it is
               },
               [n, set](const std::string& l) {
                   for (size_t i = 0; i < n.labels.size(); ++i)
                       if (n.labels[i] == l) set(n.ids[i]);
               });
    }
    void byName(const std::string& name, const std::string& label, const Named& n, std::function<std::string&()> ref) {
        byName(name, label, n, [ref] { return ref(); }, [ref](const std::string& id) { ref() = id; });
    }

private:
    JProperty make(const std::string& name, const std::string& label) const {
        JProperty p;
        p.name = name;
        p.meta.category = m_category;
        p.meta.label = label;
        return p;
    }

    JPropertyModel& m_model;
    std::string     m_category;
};

template <class T>
std::function<T&()> finder(std::vector<T>& items, const std::string& id) {
    return [&items, id]() -> T& {
        for (T& i : items)
            if (i.id == id) return i;
        return items.front();   // not reached: a form is made only for a part in the cell
    };
}

template <class T>
bool has(const std::vector<T>& items, const std::string& id) {
    for (const T& i : items)
        if (i.id == id) return true;
    return false;
}

// Where a nozzle, camera or actuator is: on which head (or none), moved by
// which axes, and its offset there. `noHead`: what having none is called.
template <class T>
void mountProperties(Adder& add, JPCellConfig& cell, std::function<T&()> part, const std::string& noHead,
                     bool fixedHasPlace, JPSetupProperties::Form& form) {
    auto mount = [part]() -> JPMountConfig& { return part().mount; };
    add.category("Where It Is");
    add.byName("head", "Head", named(cell.heads, noHead), [mount] { return mount().headId; },
               [&cell, mount](const std::string& headId) {
                   JPMountConfig& m = mount();
                   m.headId = headId;
                   if (headId.empty()) {
                       m.axisX = m.axisY = m.axisZ = m.axisRotation = "";
                       return;
                   }
                   // On a head it moves with the head: on X and Y as the head's other parts do.
                   if (!m.axisX.empty()) return;
                   auto from = [&m](const auto& items) {
                       for (const auto& i : items)
                           if (i.mount.headId == m.headId && &i.mount != &m && !i.mount.axisX.empty()) {
                               m.axisX = i.mount.axisX;
                               m.axisY = i.mount.axisY;
                               return true;
                           }
                       return false;
                   };
                   from(cell.nozzles) || from(cell.cameras) || from(cell.actuators);
               });
    form.reshaping.push_back("head");
    if (mount().headId.empty()) {
        if (!fixedHasPlace) return;
        add.number("offsetX", "X (mm)", [mount]() -> double& { return mount().offsetX; });
        add.number("offsetY", "Y (mm)", [mount]() -> double& { return mount().offsetY; });
        add.number("offsetZ", "Z in focus (mm)", [mount]() -> double& { return mount().offsetZ; });
        return;
    }
    const Named axes = named(cell.axes, "(none)");
    add.byName("axisX", "X axis", axes, [mount]() -> std::string& { return mount().axisX; });
    add.byName("axisY", "Y axis", axes, [mount]() -> std::string& { return mount().axisY; });
    add.byName("axisZ", "Z axis", axes, [mount]() -> std::string& { return mount().axisZ; });
    add.byName("axisRotation", "Rotation axis", axes, [mount]() -> std::string& { return mount().axisRotation; });
    add.number("offsetX", "Offset X (mm)", [mount]() -> double& { return mount().offsetX; });
    add.number("offsetY", "Offset Y (mm)", [mount]() -> double& { return mount().offsetY; });
    add.number("offsetZ", "Offset Z (mm)", [mount]() -> double& { return mount().offsetZ; });
}

void machineForm(JPCellConfig& cell, JPSetupProperties::Form& f) {
    f.title = "Machine";
    Adder add(f.model);
    add.category("Machine");
    add.text("name", "Name", [&cell]() -> std::string& { return cell.name; });
}

void driverForm(JPCellConfig& cell, const std::string& id, const Strings& profiles, JPSetupProperties::Form& f) {
    auto d = finder(cell.drivers, id);
    f.title = "Controller " + d().name;
    Adder add(f.model);
    add.category("Controller");
    add.text("name", "Name", [d]() -> std::string& { return d().name; });
    Strings choices{ "auto" };
    choices.insert(choices.end(), profiles.begin(), profiles.end());
    add.choice("profile", "Firmware profile", choices, [d] { return d().profile; },
               [d](const std::string& v) { d().profile = v; });

    add.category("Connection");
    // A simulated controller is for trying jplacer without a machine; it is
    // set up in the cell file.
    if (std::as_const(d().link)["type"].str() == "simulated") {
        add.text("link", "Link", [] { return std::string("simulated (set up in the cell file)"); }, nullptr);
    } else {
        add.text("port", "Serial port", [d] { return std::as_const(d().link)["port"].str(); },
                 [d](const std::string& v) { d().link["port"] = v; });
        add.integer("baud", "Baud rate", [d] { return int(std::as_const(d().link)["baud"].number()); },
                    [d](int v) { d().link["baud"] = v; }, 0, 4000000);
        add.choice("flowControl", "Flow control", { "none", "rtscts", "xonxoff" },
                   [d] { const std::string f = std::as_const(d().link)["flowControl"].str(); return f.empty() ? std::string("none") : f; },
                   [d](const std::string& v) { d().link["flowControl"] = v == "none" ? std::string() : v; });
    }
    add.category("Timing");
    add.integer("statusIntervalMs", "Ask for status every (ms)", [d]() -> int& { return d().statusIntervalMs; }, 10, 10000);
    add.integer("commandTimeoutMs", "Command timeout (ms)", [d]() -> int& { return d().commandTimeoutMs; }, 100, 600000);
    add.integer("identifyTimeoutMs", "Identify timeout (ms)", [d]() -> int& { return d().identifyTimeoutMs; }, 100, 60000);
    add.integer("homeTimeoutMs", "Home timeout (ms)", [d]() -> int& { return d().homeTimeoutMs; }, 1000, 600000);
    add.integer("connectWaitMs", "Wait after opening (ms)", [d]() -> int& { return d().connectWaitMs; }, 0, 60000);
}

void axisForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    using A = JPAxisConfig;
    auto a = finder(cell.axes, id);
    f.title = "Axis " + a().name;
    Adder add(f.model);
    add.category("Axis");
    add.text("name", "Name", [a]() -> std::string& { return a().name; });
    // A controller axis is one a controller drives; a mapped one follows
    // another through a straight-line map (a second Z driven the other way);
    // a virtual one is only a number jplacer keeps.
    const Strings kinds{ "controller", "mapped", "virtual" };
    add.choice("kind", "Kind", kinds, [a] { return std::string(A::kindName(a().kind)); },
               [a](const std::string& v) {
                   for (A::Kind k : { A::Kind::Controller, A::Kind::Mapped, A::Kind::Virtual })
                       if (v == A::kindName(k)) a().kind = k;
               });
    f.reshaping.push_back("kind");
    add.choice("type", "Type", { "x", "y", "z", "rotation" }, [a] { return std::string(A::typeName(a().type)); },
               [a](const std::string& v) {
                   for (A::Type t : { A::Type::X, A::Type::Y, A::Type::Z, A::Type::Rotation })
                       if (v == A::typeName(t)) a().type = t;
               });
    if (a().kind == A::Kind::Controller) {
        add.category("Controller");
        add.byName("driver", "Controller", named(cell.drivers, "(none)"), [a]() -> std::string& { return a().driverId; });
        add.text("letter", "Axis letter", [a]() -> std::string& { return a().letter; });
    }
    if (a().kind == A::Kind::Mapped) {
        add.category("Follows");
        Named others = named(cell.axes, "(none)");
        add.byName("inputAxis", "Axis it follows", others, [a]() -> std::string& { return a().inputAxisId; });
        add.number("mapInput0", "Point A: at", [a]() -> double& { return a().mapInput0; });
        add.number("mapOutput0", "Point A: this axis at", [a]() -> double& { return a().mapOutput0; });
        add.number("mapInput1", "Point B: at", [a]() -> double& { return a().mapInput1; });
        add.number("mapOutput1", "Point B: this axis at", [a]() -> double& { return a().mapOutput1; });
    }
    add.category("Homing and Limits");
    add.number("homeCoordinate", "Home coordinate", [a]() -> double& { return a().homeCoordinate; });
    add.flag("softLimitLowEnabled", "Low soft limit on", [a]() -> bool& { return a().softLimitLowEnabled; });
    add.number("softLimitLow", "Low soft limit", [a]() -> double& { return a().softLimitLow; });
    add.flag("softLimitHighEnabled", "High soft limit on", [a]() -> bool& { return a().softLimitHighEnabled; });
    add.number("softLimitHigh", "High soft limit", [a]() -> double& { return a().softLimitHigh; });
    add.flag("safeZoneLowEnabled", "Safe zone low end on", [a]() -> bool& { return a().safeZoneLowEnabled; });
    add.number("safeZoneLow", "Safe zone low end", [a]() -> double& { return a().safeZoneLow; });
    add.flag("safeZoneHighEnabled", "Safe zone high end on", [a]() -> bool& { return a().safeZoneHighEnabled; });
    add.number("safeZoneHigh", "Safe zone high end", [a]() -> double& { return a().safeZoneHigh; });
    add.category("Motion");
    add.number("feedratePerSecond", "Top speed (per second)", [a]() -> double& { return a().feedratePerSecond; }, 1);
    add.choice("backlash", "Backlash", { "none", "one-sided" },
               [a] { return std::string(a().backlash == A::Backlash::OneSided ? "one-sided" : "none"); },
               [a](const std::string& v) { a().backlash = v == "one-sided" ? A::Backlash::OneSided : A::Backlash::None; });
    add.number("backlashOffset", "Backlash offset", [a]() -> double& { return a().backlashOffset; });
    add.number("backlashSpeedFactor", "Backlash final approach (share of speed)",
               [a]() -> double& { return a().backlashSpeedFactor; }, 2);
}

void headForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto h = finder(cell.heads, id);
    f.title = "Head " + h().name;
    Adder add(f.model);
    add.category("Head");
    add.text("name", "Name", [h]() -> std::string& { return h().name; });
    // A place kept as "none" until it is set.
    auto place = [&add, &f](const std::string& key, const std::string& what, std::function<std::optional<JPLocation>&()> at,
                            bool withZ) {
        add.flag(key, what + " set", [at] { return at().has_value(); },
                 [at](bool on) {
                     if (!on) at().reset();
                     else if (!at()) at() = JPLocation();
                 });
        f.reshaping.push_back(key);
        if (!at()) return;
        add.number(key + "X", what + " X (mm)", [at]() -> double& { return at()->x; });
        add.number(key + "Y", what + " Y (mm)", [at]() -> double& { return at()->y; });
        if (withZ) add.number(key + "Z", what + " Z (mm)", [at]() -> double& { return at()->z; });
    };
    add.category("Homing Mark");
    place("homingFiducial", "Homing mark", [h]() -> std::optional<JPLocation>& { return h().homingFiducial; }, true);
    if (h().homingFiducial) {
        add.number("homingFiducialDiameter", "Homing mark diameter (mm)", [h]() -> double& { return h().homingFiducialDiameter; });
        add.flag("visualHoming", "Home with the camera", [h]() -> bool& { return h().visualHoming; });
    }
    add.category("Park");
    place("park", "Park place", [h]() -> std::optional<JPLocation>& { return h().park; }, false);
}

void nozzleForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto n = finder(cell.nozzles, id);
    f.title = "Nozzle " + n().name;
    Adder add(f.model);
    add.category("Nozzle");
    add.text("name", "Name", [n]() -> std::string& { return n().name; });
    mountProperties<JPNozzleConfig>(add, cell, n, "(none)", false, f);
}

void cameraForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto c = finder(cell.cameras, id);
    f.title = "Camera " + c().name;
    Adder add(f.model);
    add.category("Camera");
    add.text("name", "Name", [c]() -> std::string& { return c().name; });
    add.choice("looking", "Looking", { "down", "up" }, [c] { return std::string(c().looksUp ? "up" : "down"); },
               [c](const std::string& v) { c().looksUp = v == "up"; });
    mountProperties<JPCameraConfig>(add, cell, c, "(fixed to the machine)", true, f);

    add.category("Picture");
    auto device = [c]() -> JJson& { return c().device; };
    if (std::as_const(device())["backend"].str() == "simulated") {
        add.text("backend", "Camera", [] { return std::string("simulated (set up in the cell file)"); }, nullptr);
    } else {
        // Found by the name the device gives itself, whichever socket it is in.
        add.text("device", "Device name", [device] { return std::as_const(device())["name"].str(); },
                 [device](const std::string& v) { device()["name"] = v; });
    }
    // Width and height 0: the largest picture the camera offers.
    add.choice("format", "Format", { "any", "MJPG", "YUYV" },
               [device] { const std::string v = std::as_const(device())["format"].str(); return v.empty() ? std::string("any") : v; },
               [device](const std::string& v) { device()["format"] = v == "any" ? std::string() : v; });
    add.integer("width", "Width (0: the largest)", [device] { return int(std::as_const(device())["width"].number()); },
                [device](int v) { device()["width"] = v; }, 0, 10000);
    add.integer("height", "Height (0: the largest)", [device] { return int(std::as_const(device())["height"].number()); },
                [device](int v) { device()["height"] = v; }, 0, 10000);
    add.byName("light", "Light", named(cell.actuators, "(none)"), [device] { return std::as_const(device())["light-actuator-id"].str(); },
               [device](const std::string& v) { device()["light-actuator-id"] = v; });
    add.category("Scale");
    // A start for calibrating with a nozzle's tip, whose size is not known.
    add.number("unitsPerPixelX", "Rough mm per pixel X", [c]() -> double& { return c().unitsPerPixelX; }, 5);
    add.number("unitsPerPixelY", "Rough mm per pixel Y", [c]() -> double& { return c().unitsPerPixelY; }, 5);
}

void actuatorForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto a = finder(cell.actuators, id);
    f.title = "Actuator " + a().name;
    Adder add(f.model);
    add.category("Actuator");
    add.text("name", "Name", [a]() -> std::string& { return a().name; });
    add.byName("head", "Head", named(cell.heads, "(on the machine)"), [a]() -> std::string& { return a().mount.headId; });
    f.reshaping.push_back("head");
    add.category("Commands");
    add.byName("driver", "Controller", named(cell.drivers, "(none)"), [a]() -> std::string& { return a().driverId; });
    // {index} in a command is replaced by the index.
    add.text("index", "Index", [a]() -> std::string& { return a().index; });
    add.text("onCommand", "On command", [a]() -> std::string& { return a().onCommand; });
    add.text("offCommand", "Off command", [a]() -> std::string& { return a().offCommand; });
    add.text("readCommand", "Read command", [a]() -> std::string& { return a().readCommand; });
    add.text("readPattern", "Reply pattern", [a]() -> std::string& { return a().readPattern; });
    add.text("unit", "Unit of what is read", [a]() -> std::string& { return a().unit; });
}

} // namespace

JPSetupProperties::Form JPSetupProperties::forNode(JPCellConfig& cell, const std::string& path, const Strings& profiles) {
    Form f;
    const JPSetupTree::Path p = JPSetupTree::parse(path);
    if (p.kind == "machine") machineForm(cell, f);
    else if (p.kind == "driver" && has(cell.drivers, p.id)) driverForm(cell, p.id, profiles, f);
    else if (p.kind == "axis" && has(cell.axes, p.id)) axisForm(cell, p.id, f);
    else if (p.kind == "head" && has(cell.heads, p.id)) headForm(cell, p.id, f);
    else if (p.kind == "nozzle" && has(cell.nozzles, p.id)) nozzleForm(cell, p.id, f);
    else if (p.kind == "camera" && has(cell.cameras, p.id)) cameraForm(cell, p.id, f);
    else if (p.kind == "actuator" && has(cell.actuators, p.id)) actuatorForm(cell, p.id, f);
    return f;
}

} // inline namespace jf
