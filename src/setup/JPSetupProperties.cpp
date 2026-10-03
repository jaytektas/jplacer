// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupProperties.h"

#include "JPSetupTree.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// Adds a part's settings to a form: each to the model, and to the layout
// at the tab and group being filled, on a row of its own or on the row
// begun. Each reads and writes the cell through closures that find the part
// afresh each time (the cell's lists can move); the "ref" forms take a
// reference to a member of it.
class Adder {
public:
    using Form  = JPSetupProperties::Form;
    using Row   = JPSetupProperties::Row;
    using Place = JPSetupProperties::Place;

    explicit Adder(Form& f) : m_form(f) {}

    void tab(const std::string& title) {
        m_form.tabs.push_back({ title, {} });
    }
    void group(const std::string& title) {
        if (m_form.tabs.empty()) tab("Configuration");
        m_form.tabs.back().groups.push_back({ title, {} });
    }
    // The settings added until end() go on one row, side by side.
    void row(const std::string& label, Place place = Place::None, const std::string& axis = "") {
        Row r;
        r.label = label;
        r.place = place;
        r.axis = axis;
        rows().push_back(std::move(r));
        m_open = true;
    }
    void end() { m_open = false; }
    // An empty place on the row begun (a column this row has nothing in).
    void skip() { rows().back().cells.push_back({ "", "" }); }
    void header(const Strings& titles) {
        Row r;
        r.kind = Row::Kind::Header;
        for (const std::string& t : titles) r.cells.push_back({ "", t });
        rows().push_back(std::move(r));
    }
    void note(const std::string& text) {
        Row r;
        r.kind = Row::Kind::Note;
        r.text = text;
        rows().push_back(std::move(r));
    }
    // Buttons: (label, action) each; the owner does the action.
    void actions(const std::vector<std::pair<std::string, std::string>>& buttons) {
        Row r;
        r.kind = Row::Kind::Actions;
        for (const auto& [label, action] : buttons) r.cells.push_back({ action, label });
        rows().push_back(std::move(r));
    }

    // `placeholder`: what an empty value stands for, shown greyed.
    void text(const std::string& name, const std::string& label, std::function<std::string()> get,
              std::function<void(const std::string&)> set, const std::string& editor = "",
              const std::string& placeholder = "") {
        JProperty p = make(name, label);
        p.meta.editor = editor;
        p.meta.def = placeholder;
        p.get = [get] { return JVariant(get()); };
        if (set) p.set = [set](const JVariant& v) { set(v.toString()); return true; };   // none: shown, not edited
        put(std::move(p));
    }
    void text(const std::string& name, const std::string& label, std::function<std::string&()> ref,
              const std::string& editor = "") {
        text(name, label, [ref] { return ref(); }, [ref](const std::string& v) { ref() = v; }, editor);
    }
    void number(const std::string& name, const std::string& label, std::function<double()> get,
                std::function<void(double)> set, int decimals = 3) {
        JProperty p = make(name, label);
        p.meta.decimals = decimals;
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(v.toDouble()); return true; };
        put(std::move(p));
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
        put(std::move(p));
    }
    void integer(const std::string& name, const std::string& label, std::function<int&()> ref, int min, int max) {
        integer(name, label, [ref] { return ref(); }, [ref](int v) { ref() = v; }, min, max);
    }
    void flag(const std::string& name, const std::string& label, std::function<bool()> get, std::function<void(bool)> set) {
        JProperty p = make(name, label);
        p.get = [get] { return JVariant(get()); };
        p.set = [set](const JVariant& v) { set(v.toBool()); return true; };
        put(std::move(p));
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
        put(std::move(p));
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
    std::vector<Row>& rows() {
        if (m_form.tabs.empty() || m_form.tabs.back().groups.empty()) group("");
        return m_form.tabs.back().groups.back().rows;
    }
    JProperty make(const std::string& name, const std::string& label) const {
        JProperty p;
        p.name = name;
        p.meta.category = m_form.tabs.empty() || m_form.tabs.back().groups.empty() ? "" : m_form.tabs.back().groups.back().title;
        p.meta.label = label;
        return p;
    }
    void put(JProperty p) {
        const std::string name = p.name, label = p.meta.label;
        m_form.model.add(std::move(p));
        if (m_open) {
            Row& r = rows().back();
            r.cells.push_back({ name, r.cells.empty() ? "" : label });
            return;
        }
        Row r;
        r.label = label;
        r.cells.push_back({ name, "" });
        rows().push_back(std::move(r));
    }

    Form& m_form;
    bool  m_open = false;
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

using Place = JPSetupProperties::Place;
const Strings kXYZR{ "X", "Y", "Z", "Rotation" };

// Where a nozzle, camera or actuator is: on which head (or none), moved by
// which axes, and its offset there: OpenPnP's Coordinate System group, the
// axes and offsets as columns. `noHead`: what having none is called; a
// fixed part with a place (a camera looking up) has a Location instead.
template <class T>
void coordinateSystem(Adder& add, JPCellConfig& cell, std::function<T&()> part, const std::string& noHead,
                      bool fixedHasPlace, JPSetupProperties::Form& form) {
    auto mount = [part]() -> JPMountConfig& { return part().mount; };
    add.group(mount().headId.empty() && fixedHasPlace ? "Location" : "Coordinate System");
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
        // Where it is on the machine; Z where a part is in focus over it.
        add.header({ "X", "Y", "Z" });
        add.row("Location", Place::Location);
        add.number("offsetX", "X", [mount]() -> double& { return mount().offsetX; });
        add.number("offsetY", "Y", [mount]() -> double& { return mount().offsetY; });
        add.number("offsetZ", "Z", [mount]() -> double& { return mount().offsetZ; });
        add.end();
        return;
    }
    const Named axes = named(cell.axes, "(none)");
    add.header(kXYZR);
    add.row("Axis");
    add.byName("axisX", "X axis", axes, [mount]() -> std::string& { return mount().axisX; });
    add.byName("axisY", "Y axis", axes, [mount]() -> std::string& { return mount().axisY; });
    add.byName("axisZ", "Z axis", axes, [mount]() -> std::string& { return mount().axisZ; });
    add.byName("axisRotation", "Rotation axis", axes, [mount]() -> std::string& { return mount().axisRotation; });
    add.end();
    add.row("Offset");
    add.number("offsetX", "Offset X", [mount]() -> double& { return mount().offsetX; });
    add.number("offsetY", "Offset Y", [mount]() -> double& { return mount().offsetY; });
    add.number("offsetZ", "Offset Z", [mount]() -> double& { return mount().offsetZ; });
    add.end();
}

void machineForm(JPCellConfig& cell, JPSetupProperties::Form& f) {
    f.title = "Machine";
    Adder add(f);
    add.tab("Configuration");
    add.group("General");
    add.text("name", "Name", [&cell]() -> std::string& { return cell.name; }, "name");
    add.flag("homeAfterConnect", "Home after connected?", [&cell]() -> bool& { return cell.homeAfterConnect; });
    add.flag("parkAfterHome", "Park after homed?", [&cell]() -> bool& { return cell.parkAfterHome; });
    add.group("Locations");
    add.header({ "X", "Y", "Z", "Rotation", "Set?" });
    auto at = [&cell]() -> std::optional<JPLocation>& { return cell.discardLocation; };
    add.row("Discard Location", at() ? Place::Location : Place::None);
    if (at()) {
        add.number("discardX", "Discard X", [at]() -> double& { return at()->x; });
        add.number("discardY", "Discard Y", [at]() -> double& { return at()->y; });
        add.number("discardZ", "Discard Z", [at]() -> double& { return at()->z; });
        add.number("discardRotation", "Discard Rotation", [at]() -> double& { return at()->rotation; });
    } else {
        for (int i = 0; i < 4; ++i) add.skip();
    }
    add.flag("discard", "Set?", [at] { return at().has_value(); },
             [at](bool on) {
                 if (!on) at().reset();
                 else if (!at()) at() = JPLocation();
             });
    add.end();
    f.reshaping.push_back("discard");
    add.note("Where a nozzle drops a part that is not wanted.");
}

void driverForm(JPCellConfig& cell, const std::string& id, const std::vector<JPFirmwareProfile>& profiles, JPSetupProperties::Form& f) {
    auto d = finder(cell.drivers, id);
    f.title = "Controller " + d().name;
    Adder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.text("name", "Name", [d]() -> std::string& { return d().name; }, "name");
    Strings choices{ "auto" };
    for (const JPFirmwareProfile& p : profiles) choices.push_back(p.id());
    add.choice("profile", "Firmware Profile", choices, [d] { return d().profile; },
               [d](const std::string& v) { d().profile = v; });
    // A simulated controller is for trying jplacer without a machine; it is
    // set up in the cell file.
    if (std::as_const(d().link)["type"].str() == "simulated") {
        add.group("Communications");
        add.text("link", "Link", [] { return std::string("simulated (set up in the cell file)"); }, nullptr);
    } else {
        add.group("Communications");
        auto linkText = [d](const char* key, const std::string& none) {
            return [d, key, none] { const std::string v = std::as_const(d().link)[key].str(); return v.empty() ? none : v; };
        };
        add.choice("lineEnding", "Line-Endings", { "LF", "CR", "CRLF" }, linkText("lineEnding", "LF"),
                   [d](const std::string& v) { d().link["lineEnding"] = v; });
        add.group("Serial Port");
        add.text("port", "Port", [d] { return std::as_const(d().link)["port"].str(); },
                 [d](const std::string& v) { d().link["port"] = v; }, "long");
        // The rates a serial port takes.
        Strings rates;
        for (int r : { 1200, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 921600 }) rates.push_back(std::to_string(r));
        add.choice("baud", "Baud", rates, [d] { return std::to_string(int(std::as_const(d().link)["baud"].number())); },
                   [d](const std::string& v) { d().link["baud"] = std::atoi(v.c_str()); });
        add.choice("parity", "Parity", { "none", "even", "odd" }, linkText("parity", "none"),
                   [d](const std::string& v) { d().link["parity"] = v; });
        add.choice("dataBits", "Data Bits", { "5", "6", "7", "8" },
                   [d] { return std::to_string(int(std::as_const(d().link)["dataBits"].number(8))); },
                   [d](const std::string& v) { d().link["dataBits"] = std::atoi(v.c_str()); });
        add.choice("stopBits", "Stop Bits", { "1", "2" },
                   [d] { return std::to_string(int(std::as_const(d().link)["stopBits"].number(1))); },
                   [d](const std::string& v) { d().link["stopBits"] = std::atoi(v.c_str()); });
        add.choice("flowControl", "Flow Control", { "none", "rtscts", "xonxoff" }, linkText("flowControl", "none"),
                   [d](const std::string& v) { d().link["flowControl"] = v == "none" ? std::string() : v; });
        add.flag("setDtr", "Set DTR", [d] { return std::as_const(d().link)["setDtr"].boolean(); },
                 [d](bool v) { d().link["setDtr"] = v; });
        add.flag("setRts", "Set RTS", [d] { return std::as_const(d().link)["setRts"].boolean(); },
                 [d](bool v) { d().link["setRts"] = v; });
        add.note("Connection settings are used the next time the machine is connected.");
    }
    add.tab("Driver Settings");
    add.group("Settings");
    add.number("maxFeedRate", "Max. Feed Rate [/min]", [d]() -> double& { return d().maxFeedRate; }, 0);
    add.flag("logGcode", "Log G-code?", [d]() -> bool& { return d().logGcode; });
    add.integer("commandTimeoutMs", "Command Timeout [ms]", [d]() -> int& { return d().commandTimeoutMs; }, 100, 600000);
    add.integer("connectWaitMs", "Connect Wait Time [ms]", [d]() -> int& { return d().connectWaitMs; }, 0, 60000);
    add.integer("identifyTimeoutMs", "Identify Timeout [ms]", [d]() -> int& { return d().identifyTimeoutMs; }, 100, 60000);
    add.integer("homeTimeoutMs", "Home Timeout [ms]", [d]() -> int& { return d().homeTimeoutMs; }, 1000, 600000);
    add.integer("statusIntervalMs", "Status Interval [ms]", [d]() -> int& { return d().statusIntervalMs; }, 10, 10000);
    add.note("Max. Feed Rate 0: moves are as fast as their axes allow.");

    // The firmware's commands, each replaceable for this controller (a
    // machine wired its own way homes its own way). Empty: the profile's.
    add.tab("Gcode");
    add.group("Gcode");
    std::map<std::string, Strings> templates;   // command: the profiles' templates for it
    for (const JPFirmwareProfile& p : profiles)
        if (d().profile == "auto" || d().profile == p.id())
            for (const auto& [name, text] : p.commands()) templates[name].push_back(p.id() + ": " + text);
    for (const auto& [name, _] : d().commands) templates[name];
    for (const auto& [name, those] : templates) {
        std::string def;
        for (const std::string& t : those) def += (def.empty() ? "" : "   ") + t;
        add.text("command:" + name, name, [d, name] { const auto i = d().commands.find(name); return i == d().commands.end() ? std::string() : i->second; },
                 [d, name](const std::string& v) {
                     if (v.empty()) d().commands.erase(name);
                     else d().commands[name] = v;
                 }, "lines", def);
    }
    add.note("Empty: the firmware profile's command, shown greyed. A command can be several lines; {placeholders} are filled in.");
}

void axisForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    using A = JPAxisConfig;
    auto a = finder(cell.axes, id);
    f.title = "Axis " + a().name;
    Adder add(f);
    add.tab("Configuration");
    add.group("Properties");
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
    add.text("name", "Name", [a]() -> std::string& { return a().name; }, "name");
    if (a().kind == A::Kind::Controller) {
        add.group("Controller Settings");
        add.byName("driver", "Driver", named(cell.drivers, "(none)"), [a]() -> std::string& { return a().driverId; });
        add.text("letter", "Axis Letter", [a]() -> std::string& { return a().letter; });
        add.number("homeCoordinate", "Home Coordinate", [a]() -> double& { return a().homeCoordinate; });
        // One motor step, and its other side: steps per unit.
        const std::string unit = a().type == A::Type::Rotation ? "Degree" : "Millimeter";
        add.row(a().type == A::Type::Rotation ? "Resolution [Degrees]" : "Resolution [Millimeters]");
        add.number("resolution", "Resolution", [a]() -> double& { return a().resolution; }, 6);
        add.number("stepsPerUnit", "Steps / " + unit, [a] { return a().resolution > 0 ? 1 / a().resolution : 0.0; },
                   [a](double v) { a().resolution = v > 0 ? 1 / v : 0; }, 6);
        add.end();
        if (a().type == A::Type::Rotation) {
            add.flag("limitRotation", "Limit to Range", [a]() -> bool& { return a().limitRotation; });
            add.flag("wrapAroundRotation", "Wrap Around", [a]() -> bool& { return a().wrapAroundRotation; });
            add.note("Limit to Range keeps the angle within -180..180; Wrap Around turns the short way round.");
        }
    }
    if (a().kind == A::Kind::Virtual) {
        add.group("Virtual Axis");
        add.number("homeCoordinate", "Home / Safe Z", [a]() -> double& { return a().homeCoordinate; });
    }
    if (a().kind == A::Kind::Mapped) {
        add.group("Axis Mapping");
        add.byName("inputAxis", "Input Axis", named(cell.axes, "(none)"), [a]() -> std::string& { return a().inputAxisId; });
        add.header({ "Input", "Output" });
        add.row("Map Point A");
        add.number("mapInput0", "Point A input", [a]() -> double& { return a().mapInput0; });
        add.number("mapOutput0", "Point A output", [a]() -> double& { return a().mapOutput0; });
        add.end();
        add.row("Map Point B");
        add.number("mapInput1", "Point B input", [a]() -> double& { return a().mapInput1; });
        add.number("mapOutput1", "Point B output", [a]() -> double& { return a().mapOutput1; });
        add.end();
        add.number("homeCoordinate", "Home Coordinate", [a]() -> double& { return a().homeCoordinate; });
    }
    add.group("Kinematic Settings");
    // Each limit with its switch, and buttons to take it from where the axis is or go there.
    auto limit = [&add, a, id](const std::string& key, const std::string& label, double A::*value, bool A::*on) {
        add.row(label, Place::Axis, id);
        add.number(key, label, [a, value]() -> double& { return a().*value; });
        add.flag(key + "Enabled", "Enabled?", [a, on]() -> bool& { return a().*on; });
        add.end();
    };
    limit("softLimitLow", "Soft Limit Low", &A::softLimitLow, &A::softLimitLowEnabled);
    limit("safeZoneLow", "Safe Zone Low", &A::safeZoneLow, &A::safeZoneLowEnabled);
    limit("safeZoneHigh", "Safe Zone High", &A::safeZoneHigh, &A::safeZoneHighEnabled);
    limit("softLimitHigh", "Soft Limit High", &A::softLimitHigh, &A::softLimitHighEnabled);
    add.row("Feed Rate [/s]");
    add.number("feedratePerSecond", "Feed Rate [/s]", [a]() -> double& { return a().feedratePerSecond; }, 1);
    add.number("feedratePerMinute", "Feed Rate [/min]", [a] { return a().feedratePerSecond * 60; },
               [a](double v) { a().feedratePerSecond = v / 60; }, 1);
    add.end();
    add.number("accelerationPerSecond2", "Acceleration [/s\u00B2]", [a]() -> double& { return a().accelerationPerSecond2; }, 1);
    add.number("jerkPerSecond3", "Jerk [/s\u00B3]", [a]() -> double& { return a().jerkPerSecond3; }, 1);
    add.note("0: the controller's own. Acceleration and jerk reach a controller whose move command takes "
             "{acceleration} or {jerk} (its Gcode tab).");

    add.tab("Backlash Compensation");
    add.group("Backlash Compensation");
    add.choice("backlash", "Compensation Method", { "None", "OneSidedPositioning" },
               [a] { return std::string(a().backlash == A::Backlash::OneSided ? "OneSidedPositioning" : "None"); },
               [a](const std::string& v) { a().backlash = v == "OneSidedPositioning" ? A::Backlash::OneSided : A::Backlash::None; });
    f.reshaping.push_back("backlash");
    if (a().backlash == A::Backlash::OneSided) {
        add.number("backlashOffset", "Backlash Offset", [a]() -> double& { return a().backlashOffset; });
        add.number("backlashSpeedFactor", "Speed Factor", [a]() -> double& { return a().backlashSpeedFactor; }, 2);
        add.note("Every move ends coming from the same side: past the place by the offset, then back at the speed factor.");
    }
}

void headForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto h = finder(cell.heads, id);
    f.title = "Head " + h().name;
    Adder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.text("name", "Name", [h]() -> std::string& { return h().name; }, "name");
    add.group("Locations");
    // A place kept as "none" until it is set: a box to set it, then its coordinates.
    auto place = [&add, &f](const std::string& key, const std::string& what, std::function<std::optional<JPLocation>&()> at,
                            bool withZ) {
        add.row(what, at() ? Place::Location : Place::None);
        if (at()) {
            add.number(key + "X", what + " X", [at]() -> double& { return at()->x; });
            add.number(key + "Y", what + " Y", [at]() -> double& { return at()->y; });
            if (withZ) add.number(key + "Z", what + " Z", [at]() -> double& { return at()->z; });
            else add.skip();
        } else {
            add.skip();
            add.skip();
            add.skip();
        }
        add.flag(key, "Set?", [at] { return at().has_value(); },
                 [at](bool on) {
                     if (!on) at().reset();
                     else if (!at()) at() = JPLocation();
                 });
        add.end();
        f.reshaping.push_back(key);
    };
    add.header({ "X", "Y", "Z", "Set?" });
    place("homingFiducial", "Homing Fiducial", [h]() -> std::optional<JPLocation>& { return h().homingFiducial; }, true);
    if (h().homingFiducial) {
        add.number("homingFiducialDiameter", "Fiducial Diameter", [h]() -> double& { return h().homingFiducialDiameter; });
        add.row("Homing Method");
        add.choice("visualHoming", "Homing Method", { "Switches", "ResetToFiducialLocation" },
                   [h] { return std::string(h().visualHoming ? "ResetToFiducialLocation" : "Switches"); },
                   [h](const std::string& v) { h().visualHoming = v == "ResetToFiducialLocation"; });
        add.end();
        add.actions({ { "Visual Test", "visualTest" }, { "Visual Home", "visualHome" } });
        add.note("Set the homing fiducial up early, before capturing many places: each time it changes or moves, "
                 "every place captured since is off by as much.");
    }
    place("park", "Park Location", [h]() -> std::optional<JPLocation>& { return h().park; }, false);

    add.group("Pump");
    add.byName("pumpActuator", "Vacuum Pump Actuator", named(cell.actuators, "(none)"), [h]() -> std::string& { return h().pumpActuatorId; });
    add.choice("pumpControl", "Pump Control", { "None", "PartOn", "TaskDuration", "KeepRunning" },
               [h] { return h().pumpControl.empty() ? std::string("None") : h().pumpControl; },
               [h](const std::string& v) { h().pumpControl = v; });
    add.integer("pumpOnWaitMs", "Pump On Wait [ms]", [h]() -> int& { return h().pumpOnWaitMs; }, 0, 600000);
    add.note("PartOn: on while a nozzle holds a part. TaskDuration: on for the work, off with the last part. "
             "KeepRunning: once on, left on.");
}

void nozzleForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto n = finder(cell.nozzles, id);
    f.title = "Nozzle " + n().name;
    Adder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.text("name", "Name", [n]() -> std::string& { return n().name; }, "name");
    coordinateSystem<JPNozzleConfig>(add, cell, n, "(none)", false, f);
    add.group("Settings");
    add.integer("pickDwellMs", "Pick Dwell Time (ms)", [n]() -> int& { return n().pickDwellMs; }, 0, 60000);
    add.integer("placeDwellMs", "Place Dwell Time (ms)", [n]() -> int& { return n().placeDwellMs; }, 0, 60000);
    add.note("The total dwell is the nozzle's and its tip's together.");

    // Every tip: whether it fits this nozzle, and which one is on it now.
    add.tab("Nozzle Tips");
    add.group("Nozzle Tips");
    add.header({ "Compatible?", "Loaded?" });
    for (const JPNozzleTipConfig& t : cell.nozzleTips) {
        const std::string tid = t.id;
        add.row(t.name.empty() ? t.id : t.name);
        add.flag("fits:" + tid, "Compatible?", [n, tid] { return n().fits(tid); },
                 [n, tid](bool on) {
                     JPNozzleConfig& z = n();
                     std::erase(z.tipIds, tid);
                     if (on) z.tipIds.push_back(tid);
                     else if (z.tipId == tid) z.tipId.clear();   // a tip that does not fit is not on it
                 });
        add.flag("loaded:" + tid, "Loaded?", [n, tid] { return n().tipId == tid; },
                 [&cell, n, tid](bool on) {
                     JPNozzleConfig& z = n();
                     if (!on) {
                         if (z.tipId == tid) z.tipId.clear();
                         return;
                     }
                     // On this nozzle: it fits it, and it is on no other.
                     if (!z.fits(tid)) z.tipIds.push_back(tid);
                     for (JPNozzleConfig& other : cell.nozzles)
                         if (other.tipId == tid) other.tipId.clear();
                     z.tipId = tid;
                 });
        add.end();
    }
    if (cell.nozzleTips.empty()) add.note("No nozzle tips yet: add them under Nozzle Tips.");
    add.note("Loaded? says which tip is on the nozzle now; ticking it moves nothing.");

    add.tab("Vacuum");
    add.group("Vacuum");
    const Named actuators = named(cell.actuators, "(none)");
    add.byName("vacuumActuator", "Vacuum Actuator", actuators, [n]() -> std::string& { return n().vacuumActuatorId; });
    add.row("Blow Off Actuator");
    add.byName("blowOffActuator", "Blow Off Actuator", actuators, [n]() -> std::string& { return n().blowOffActuatorId; });
    add.flag("blowOffClosesVacuum", "Closes Vacuum Actuator?", [n]() -> bool& { return n().blowOffClosesVacuum; });
    add.end();
    add.byName("vacuumSenseActuator", "Sensing Actuator", actuators, [n]() -> std::string& { return n().vacuumSenseActuatorId; });
    add.note("Pick switches the vacuum on; Place switches it off, then pulses the blow-off for the place dwell (Jog panel).");
}

void nozzleTipForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto t = finder(cell.nozzleTips, id);
    f.title = "Nozzle tip " + t().name;
    Adder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.text("name", "Name", [t]() -> std::string& { return t().name; }, "name");
    add.group("Pick & Place");
    add.integer("pickDwellMs", "Pick Dwell Time (ms)", [t]() -> int& { return t().pickDwellMs; }, 0, 60000);
    add.integer("placeDwellMs", "Place Dwell Time (ms)", [t]() -> int& { return t().placeDwellMs; }, 0, 60000);
    add.note("Added to the nozzle's own dwell.");
    add.group("Part Dimensions");
    add.number("diameter", "Diameter Seen From Below", [t]() -> double& { return t().diameter; });
    add.group("Nozzles");
    for (const JPNozzleConfig& n : cell.nozzles) {
        auto nozzle = finder(cell.nozzles, n.id);
        add.flag("fits:" + n.id, n.name.empty() ? n.id : n.name, [nozzle, id] { return nozzle().fits(id); },
                 [nozzle, id](bool on) {
                     JPNozzleConfig& z = nozzle();
                     std::erase(z.tipIds, id);
                     if (on) z.tipIds.push_back(id);
                     else if (z.tipId == id) z.tipId.clear();   // a tip that does not fit is not on it
                 });
    }
    add.note("The nozzles this tip fits.");

    add.tab("Tool Changer");
    add.group("Nozzle Tip Changer");
    add.choice("unloading", "Unloading", { "loading backwards", "steps of its own" },
               [t] { return std::string(t().unloadReversesLoad ? "loading backwards" : "steps of its own"); },
               [t](const std::string& v) {
                   JPNozzleTipConfig& tip = t();
                   const bool backwards = v == "loading backwards";
                   // Its own start as loading backwards, to change from there.
                   if (!backwards && tip.unloadReversesLoad && tip.unloadSteps.empty())
                       tip.unloadSteps = tip.unloadingSteps();
                   tip.unloadReversesLoad = backwards;
               });
    add.note("The steps of loading and unloading are in the tree under the tip: select one to change it, "
             "Add to add one after it.");
}

// An optional coordinate as text: empty when left out (the nozzle stays as it
// is on that axis). What is not a number is not taken.
void coordinate(Adder& add, const std::string& name, const std::string& label,
                std::function<std::optional<double>&()> ref) {
    add.text(name, label, [ref] { return ref() ? JPSetupTree::shortNumber(*ref()) : std::string(); },
             [ref](const std::string& v) {
                 if (v.find_first_not_of(" \t") == std::string::npos) {
                     ref().reset();
                     return;
                 }
                 char* end = nullptr;
                 const double d = std::strtod(v.c_str(), &end);
                 if (end && end != v.c_str() && v.find_first_not_of(" \t", size_t(end - v.c_str())) == std::string::npos)
                     ref() = d;
             }, "number");
}

void stepForm(JPCellConfig& cell, const JPSetupTree::Path& p, JPSetupProperties::Form& f) {
    using S = JPChangerStep;
    auto tip = finder(cell.nozzleTips, p.owner);
    const size_t index = size_t(std::strtoul(p.id.c_str(), nullptr, 10));
    const bool load = p.list == "load";
    f.title = "Nozzle tip " + tip().name + ": " + (load ? "load" : "unload") + " step " + std::to_string(index + 1);
    Adder add(f);
    add.tab("Step");
    add.group("Step");
    if (!load && tip().unloadReversesLoad) {
        // Worked out from loading; changed by changing loading.
        const std::vector<S> steps = tip().unloadingSteps();
        if (index >= steps.size()) return;
        const std::string what = JPSetupTree::stepLabel(cell, steps[index], index);
        add.text("what", "Loading Backwards", [what] { return what.substr(what.find(' ') + 1); }, nullptr);
        return;
    }
    auto step = [tip, load, index]() -> S& {
        std::vector<S>& steps = load ? tip().loadSteps : tip().unloadSteps;
        return steps[index];
    };
    if (index >= (load ? tip().loadSteps : tip().unloadSteps).size()) return;
    const Strings kinds{ S::kindName(S::Kind::Move), S::kindName(S::Kind::SafeZ), S::kindName(S::Kind::Actuator),
                         S::kindName(S::Kind::Wait), S::kindName(S::Kind::Ask) };
    add.choice("kind", "Kind", kinds, [step] { return std::string(S::kindName(step().kind)); },
               [step](const std::string& v) {
                   for (S::Kind k : { S::Kind::Move, S::Kind::SafeZ, S::Kind::Actuator, S::Kind::Wait, S::Kind::Ask })
                       if (v == S::kindName(k)) step().kind = k;
               });
    f.reshaping.push_back("kind");
    auto speed = [&add, step] {
        add.integer("speed", "Speed [%]", [step] { return int(std::lround(step().speed * 100)); },
                    [step](int v) { step().speed = std::clamp(v, 1, 100) / 100.0; }, 1, 100);
    };
    switch (step().kind) {
        case S::Kind::Move:
            // Where the nozzle doing the change goes, as the axes have it.
            add.group("Location");
            add.header(kXYZR);
            add.row("Location", Place::Location);
            coordinate(add, "x", "X", [step]() -> std::optional<double>& { return step().x; });
            coordinate(add, "y", "Y", [step]() -> std::optional<double>& { return step().y; });
            coordinate(add, "z", "Z", [step]() -> std::optional<double>& { return step().z; });
            coordinate(add, "rotation", "Rotation", [step]() -> std::optional<double>& { return step().rotation; });
            add.end();
            add.note("Where the nozzle goes, in the axes' own coordinates. A coordinate left empty stays as it is.");
            speed();
            break;
        case S::Kind::SafeZ:
            speed();
            break;
        case S::Kind::Actuator:
            add.byName("actuator", "Actuator", named(cell.actuators, "(none)"), [step]() -> std::string& { return step().actuatorId; });
            add.choice("on", "Switch It", { "on", "off" }, [step] { return std::string(step().on ? "on" : "off"); },
                       [step](const std::string& v) { step().on = v == "on"; });
            break;
        case S::Kind::Wait:
            add.integer("waitMs", "Wait [ms]", [step]() -> int& { return step().waitMs; }, 0, 600000);
            break;
        case S::Kind::Ask:
            add.text("message", "Message", [step]() -> std::string& { return step().message; }, "long");
            break;
    }
}

void cameraForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto c = finder(cell.cameras, id);
    f.title = "Camera " + c().name;
    Adder add(f);
    add.tab("General Configuration");
    add.group("Properties");
    add.text("name", "Name", [c]() -> std::string& { return c().name; }, "name");
    add.choice("looking", "Looking", { "Down", "Up" }, [c] { return std::string(c().looksUp ? "Up" : "Down"); },
               [c](const std::string& v) { c().looksUp = v == "Up"; });
    auto device = [c]() -> JJson& { return c().device; };
    add.group("Light");
    add.byName("light", "Light Actuator", named(cell.actuators, "(none)"), [device] { return std::as_const(device())["light-actuator-id"].str(); },
               [device](const std::string& v) { device()["light-actuator-id"] = v; });
    add.group("Units Per Pixel");
    // A start for calibrating with a nozzle's tip, whose size is not known.
    add.header({ "X", "Y" });
    add.row("Units per Pixel");
    add.number("unitsPerPixelX", "Units per Pixel X", [c]() -> double& { return c().unitsPerPixelX; }, 5);
    add.number("unitsPerPixelY", "Units per Pixel Y", [c]() -> double& { return c().unitsPerPixelY; }, 5);
    add.end();
    add.note("A rough start: calibrating measures them.");

    add.tab("Device Settings");
    add.group("Device");
    if (std::as_const(device())["backend"].str() == "simulated") {
        add.text("backend", "Device", [] { return std::string("simulated (set up in the cell file)"); }, nullptr);
    } else {
        // Found by the name the device gives itself, whichever socket it is in.
        add.text("device", "Device", [device] { return std::as_const(device())["name"].str(); },
                 [device](const std::string& v) { device()["name"] = v; }, "long");
    }
    add.choice("format", "Format", { "any", "MJPG", "YUYV" },
               [device] { const std::string v = std::as_const(device())["format"].str(); return v.empty() ? std::string("any") : v; },
               [device](const std::string& v) { device()["format"] = v == "any" ? std::string() : v; });
    add.header({ "Width", "Height" });
    add.row("Size");
    add.integer("width", "Width", [device] { return int(std::as_const(device())["width"].number()); },
                [device](int v) { device()["width"] = v; }, 0, 10000);
    add.integer("height", "Height", [device] { return int(std::as_const(device())["height"].number()); },
                [device](int v) { device()["height"] = v; }, 0, 10000);
    add.end();
    add.note("0: the largest picture the camera offers.");

    add.tab("Position");
    coordinateSystem<JPCameraConfig>(add, cell, c, "(fixed to the machine)", true, f);

    add.tab("Advanced Calibration");
    add.group("Camera Calibration");
    add.actions({ { "Start Calibration", "calibrate" } });
    // Straightened, a wide lens's picture no longer fills a rectangle.
    add.row("Crop All Invalid Pixels");
    add.integer("showAll", "Crop All Invalid Pixels", [c] { return int(std::lround(c().showAll * 100)); },
                [c](int v) { c().showAll = std::clamp(v, 0, 100) / 100.0; }, 0, 100);
    add.end();
    add.note("0 crops every pixel the straightening leaves without picture; 100 shows all of the picture, "
             "dark corners and all.");
}

void actuatorForm(JPCellConfig& cell, const std::string& id, JPSetupProperties::Form& f) {
    auto a = finder(cell.actuators, id);
    f.title = "Actuator " + a().name;
    Adder add(f);
    add.tab("Configuration");
    add.group("Properties");
    add.byName("driver", "Driver", named(cell.drivers, "(none)"), [a]() -> std::string& { return a().driverId; });
    add.text("name", "Name", [a]() -> std::string& { return a().name; }, "name");
    add.group("Coordinate System");
    add.byName("head", "Head", named(cell.heads, "(on the machine)"), [a]() -> std::string& { return a().mount.headId; });
    f.reshaping.push_back("head");
    add.group("General");
    // As the machine's state changes: once connected, once homed, before it is let go.
    add.header({ "Enabled", "Homed", "Disabled" });
    add.row("Actuation");
    add.choice("enabledActuation", "Enabled", { "AssumeUnknown", "ActuateOn", "ActuateOff" },
               [a] { return a().enabledActuation; }, [a](const std::string& v) { a().enabledActuation = v; });
    add.choice("homedActuation", "Homed", { "LeaveAsIs", "ActuateOn", "ActuateOff" },
               [a] { return a().homedActuation; }, [a](const std::string& v) { a().homedActuation = v; });
    add.choice("disabledActuation", "Disabled", { "LeaveAsIs", "ActuateOn", "ActuateOff" },
               [a] { return a().disabledActuation; }, [a](const std::string& v) { a().disabledActuation = v; });
    add.end();
    // {index} in a command is replaced by the index.
    add.text("index", "Index", [a]() -> std::string& { return a().index; });
    add.text("unit", "Unit Read", [a]() -> std::string& { return a().unit; });
    add.group("Commands");
    add.text("onCommand", "On", [a]() -> std::string& { return a().onCommand; }, "long");
    add.text("offCommand", "Off", [a]() -> std::string& { return a().offCommand; }, "long");
    add.text("readCommand", "Read", [a]() -> std::string& { return a().readCommand; }, "long");
    add.text("readPattern", "Read Reply Pattern", [a]() -> std::string& { return a().readPattern; }, "long");
    add.note("{index} in a command is replaced by the index.");
}

} // namespace

JPSetupProperties::Form JPSetupProperties::forNode(JPCellConfig& cell, const std::string& path, const std::vector<JPFirmwareProfile>& profiles) {
    Form f;
    const JPSetupTree::Path p = JPSetupTree::parse(path);
    if (p.kind == "machine") machineForm(cell, f);
    else if (p.kind == "driver" && has(cell.drivers, p.id)) driverForm(cell, p.id, profiles, f);
    else if (p.kind == "axis" && has(cell.axes, p.id)) axisForm(cell, p.id, f);
    else if (p.kind == "head" && has(cell.heads, p.id)) headForm(cell, p.id, f);
    else if (p.kind == "nozzle" && has(cell.nozzles, p.id)) nozzleForm(cell, p.id, f);
    else if (p.kind == "nozzletip" && has(cell.nozzleTips, p.id)) nozzleTipForm(cell, p.id, f);
    else if (p.kind == "step" && has(cell.nozzleTips, p.owner)) stepForm(cell, p, f);
    else if (p.kind == "camera" && has(cell.cameras, p.id)) cameraForm(cell, p.id, f);
    else if (p.kind == "actuator" && has(cell.actuators, p.id)) actuatorForm(cell, p.id, f);
    return f;
}

} // inline namespace jf
