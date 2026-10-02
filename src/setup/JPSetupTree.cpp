// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPSetupTree.h"

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
    root.children.push_back(group<JPCameraConfig>("Cameras", "group:cameras:", cell.cameras, "camera", fixed));
    root.children.push_back(group<JPActuatorConfig>("Actuators", "group:actuators:", cell.actuators, "actuator", fixed));
    return root;
}

JPSetupTree::Path JPSetupTree::parse(const std::string& path) {
    Path p;
    const size_t a = path.find(':');
    p.kind = path.substr(0, a);
    if (a == std::string::npos) return p;
    const std::string rest = path.substr(a + 1);
    if (p.kind != "group") {
        p.id = rest;
        return p;
    }
    const size_t b = rest.find(':');
    p.id = rest.substr(0, b);
    if (b != std::string::npos) p.headId = rest.substr(b + 1);
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

} // inline namespace jf
