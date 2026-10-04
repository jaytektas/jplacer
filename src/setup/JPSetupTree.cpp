// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupTree.h"

#include <cstdio>
#include <functional>

inline namespace jf {

namespace {

// A part's name, or its id where it has none: a row is never blank.
std::string labelOf(const std::string& name, const std::string& id) {
    return name.empty() ? id : name;
}

template <class T>
JPSetupTree::Node group(const std::string& label, const std::string& path, const std::vector<T>& items,
                        const std::string& itemKind, const std::function<bool(const T&)>& in) {
    JPSetupTree::Node g{ label, path, {} };
    for (const T& i : items)
        if (in(i)) g.children.push_back({ labelOf(i.name, i.id), itemKind + ":" + i.id, {} });
    return g;
}

} // namespace

std::string JPSetupTree::shortNumber(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    return s == "-0" ? "0" : s;
}

namespace {

JPSetupTree::Node steps(const JPCellConfig& cell, const std::string& label, const std::string& tipId,
                        const std::string& list, const std::vector<JPChangerStep>& items) {
    JPSetupTree::Node g{ label, "group:" + list + ":" + tipId, {} };
    for (size_t i = 0; i < items.size(); ++i)
        g.children.push_back({ JPSetupTree::stepLabel(cell, items[i], i), "step:" + tipId + ":" + list + ":" + std::to_string(i), {} });
    return g;
}

bool contains(const JPSetupTree::Node& n, const std::string& path, std::vector<std::string>& labels) {
    labels.push_back(n.label);
    if (n.path == path) return true;
    for (const JPSetupTree::Node& c : n.children)
        if (contains(c, path, labels)) return true;
    labels.pop_back();
    return false;
}

} // namespace

JPSetupTree::Node JPSetupTree::build(const JPCellConfig& cell) {
    Node root{ cell.name.empty() ? "Machine" : cell.name, "machine", {} };
    auto all = [](const auto&) { return true; };
    root.children.push_back(group<JPDriverConfig>("Controllers", "group:drivers", cell.drivers, "driver", all));
    root.children.push_back(group<JPAxisConfig>("Axes", "group:axes", cell.axes, "axis", all));
    Node heads{ "Heads", "group:heads", {} };
    for (const JPHeadConfig& h : cell.heads) {
        Node head{ labelOf(h.name, h.id), "head:" + h.id, {} };
        auto on = [id = h.id](const auto& part) { return part.mount.headId == id; };
        head.children.push_back(group<JPNozzleConfig>("Nozzles", "group:nozzles:" + h.id, cell.nozzles, "nozzle", on));
        head.children.push_back(group<JPCameraConfig>("Cameras", "group:cameras:" + h.id, cell.cameras, "camera", on));
        head.children.push_back(group<JPActuatorConfig>("Actuators", "group:actuators:" + h.id, cell.actuators, "actuator", on));
        heads.children.push_back(std::move(head));
    }
    root.children.push_back(std::move(heads));
    auto fixed = [](const auto& part) { return part.mount.headId.empty(); };
    // A nozzle rides on a head; one that names none is shown so it can be put on one.
    Node loose = group<JPNozzleConfig>("Nozzles", "group:nozzles:", cell.nozzles, "nozzle", fixed);
    if (!loose.children.empty()) root.children.push_back(std::move(loose));
    Node tips{ "Nozzle Tips", "group:nozzletips", {} };
    for (const JPNozzleTipConfig& t : cell.nozzleTips) {
        Node tip{ labelOf(t.name, t.id), "nozzletip:" + t.id, {} };
        tip.children.push_back(steps(cell, "Load", t.id, "load", t.loadSteps));
        tip.children.push_back(t.unloadReversesLoad ? steps(cell, "Unload (loading backwards)", t.id, "unload", t.unloadingSteps())
                                                    : steps(cell, "Unload", t.id, "unload", t.unloadSteps));
        tips.children.push_back(std::move(tip));
    }
    root.children.push_back(std::move(tips));
    root.children.push_back(group<JPCameraConfig>("Cameras", "group:cameras:", cell.cameras, "camera", fixed));
    root.children.push_back(group<JPActuatorConfig>("Actuators", "group:actuators:", cell.actuators, "actuator", fixed));
    // As OpenPnP's: how a job is run.
    Node processors{ "Job Processors", "group:jobprocessors", {} };
    processors.children.push_back({ "ReferencePnpJobProcessor", "jobprocessor", {} });
    root.children.push_back(std::move(processors));
    return root;
}

JPSetupTree::Path JPSetupTree::parse(const std::string& path) {
    Path p;
    const size_t a = path.find(':');
    p.kind = path.substr(0, a);
    if (a == std::string::npos) return p;
    const std::string rest = path.substr(a + 1);
    if (p.kind == "step") {
        // step:<tipId>:<list>:<index>
        const size_t b = rest.find(':'), c = rest.find(':', b == std::string::npos ? b : b + 1);
        if (c == std::string::npos) return p;
        p.owner = rest.substr(0, b);
        p.list  = rest.substr(b + 1, c - b - 1);
        p.id    = rest.substr(c + 1);
        return p;
    }
    if (p.kind != "group") {
        p.id = rest;
        return p;
    }
    const size_t b = rest.find(':');
    p.id = rest.substr(0, b);
    if (b != std::string::npos) p.owner = rest.substr(b + 1);
    return p;
}

std::string JPSetupTree::groupOf(const JPCellConfig& cell, const std::string& path) {
    const Path p = parse(path);
    if (p.kind == "group") return path;
    auto mounted = [&](const auto& items, const char* what) -> std::string {
        for (const auto& i : items)
            if (i.id == p.id) return std::string("group:") + what + ":" + i.mount.headId;
        return {};
    };
    if (p.kind == "driver")   return "group:drivers";
    if (p.kind == "axis")     return "group:axes";
    if (p.kind == "head")     return "group:heads";
    if (p.kind == "nozzletip") return "group:nozzletips";
    if (p.kind == "step")     return "group:" + p.list + ":" + p.owner;
    if (p.kind == "nozzle")   return mounted(cell.nozzles, "nozzles");
    if (p.kind == "camera")   return mounted(cell.cameras, "cameras");
    if (p.kind == "actuator") return mounted(cell.actuators, "actuators");
    return {};
}

std::vector<std::string> JPSetupTree::labelsTo(const Node& root, const std::string& path) {
    std::vector<std::string> labels;
    if (!contains(root, path, labels)) labels.clear();
    return labels;
}

std::string JPSetupTree::stepLabel(const JPCellConfig& cell, const JPChangerStep& step, size_t index) {
    using Kind = JPChangerStep::Kind;
    std::string s = std::to_string(index + 1) + ". ";
    const std::string speed = " at " + shortNumber(step.speed * 100) + "%";
    switch (step.kind) {
        case Kind::Move: {
            std::string to;
            const std::pair<const char*, const std::optional<double>*> parts[] = {
                { "X", &step.x }, { "Y", &step.y }, { "Z", &step.z }, { "rotation", &step.rotation } };
            for (const auto& [name, v] : parts)
                if (*v) to += (to.empty() ? " " : ", ") + std::string(name) + " " + shortNumber(**v);
            return s + (to.empty() ? "Move (nowhere)" : "Move to" + to) + speed;
        }
        case Kind::SafeZ: return s + "Up to safe Z" + speed;
        case Kind::Actuator: {
            std::string name = step.actuatorId;
            for (const JPActuatorConfig& a : cell.actuators)
                if (a.id == step.actuatorId) name = a.name.empty() ? a.id : a.name;
            return s + "Switch " + (name.empty() ? "(no actuator)" : name) + (step.on ? " on" : " off");
        }
        case Kind::Wait: return s + "Wait " + std::to_string(step.waitMs) + " ms";
        case Kind::Ask:  return s + "Ask: " + (step.message.empty() ? "(no message)" : step.message);
    }
    return s;
}

} // inline namespace jf
