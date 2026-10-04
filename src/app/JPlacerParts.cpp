// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerParts.h"

#include "JPlacerSettings.h"

#include "library/JPPlacementState.h"

#include <j/config/Settings.h>

#include <cstdio>

inline namespace jf {

namespace {

const std::vector<std::string>& columns() {
    static const std::vector<std::string> c = { "Designator", "State", "Part", "Value", "Package", "Footprint",
                                                "Side", "Rotation", "Supplier No.", "Manufacturer" };
    return c;
}

std::string plain(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

std::string numbers(const std::vector<JPSupplierNumber>& ns) {
    std::string out;
    for (const JPSupplierNumber& n : ns) {
        if (!out.empty()) out += ", ";
        out += n.supplier.empty() ? n.number : n.supplier + " " + n.number;
    }
    return out;
}

} // namespace

JPlacerParts::JPlacerParts(JPlacerJob& job, JSceneGraph& graph, JPlacerLayout& layout)
    : m_job(job), m_layout(layout) {
    m_panel = std::make_unique<JPPartsPanel>(graph, columns());
    const JSettings& s = JSettings::instance();
    m_panel->setView(s.get<std::string>(JPlacerSettings::kPartsView, "list") == "tree" ? JPPartsPanel::View::Tree
                                                                                      : JPPartsPanel::View::List,
                     int(s.get<double>(JPlacerSettings::kPartsGroup, 4)));   // Package, to begin with
    m_panel->onViewChanged = [](JPPartsPanel::View view, int group) {
        JSettings& s = JSettings::instance();
        s.set(JPlacerSettings::kPartsView, std::string(view == JPPartsPanel::View::Tree ? "tree" : "list"));
        s.set(JPlacerSettings::kPartsGroup, double(group));
        JPlacerSettings::save();
    };
    m_panel->onChosen = [this](const std::vector<std::string>& keys) {
        m_chosen = keys;
        detail();
    };
    m_dock = std::make_unique<JDockWidget>(kTitle, 0.f, 0.f, 0.f, 0.f);
    m_dock->setContent(m_panel.get());
    m_layout.add(m_dock.get(), JPlacerLayout::Home::Work);
    m_watch = m_job.watch([this](JPlacerJob::Change) { show(); });
    show();
}

JPlacerParts::~JPlacerParts() {
    m_job.unwatch(m_watch);
    m_layout.remove(m_dock.get());
    m_dock->setContent(nullptr);
}

void JPlacerParts::showDock() {
    m_layout.show(m_dock.get());
}

void JPlacerParts::show() {
    if (!m_panel) return;
    const JPJob& job = m_job.job();
    std::vector<JPPartsPanel::Row> rows;
    for (const JPPlacement& p : job.board.placements) {
        const JPPlacementState state = JPPlacementState::of(p, job.parts);
        const JPPart* part = job.parts.part(p.partId);
        const JPPackage* package = part ? job.parts.package(part->packageId) : nullptr;
        const JPFootprint* footprint = package ? job.parts.footprint(package->footprintId) : nullptr;
        rows.push_back({ p.designator,
                         { p.designator, JPPlacementState::name(state.kind), part ? part->label() : std::string(),
                           part ? part->value : p.value, package ? package->name : std::string(),
                           footprint ? footprint->name : p.footprint,
                           p.side == JPPlacement::Side::Bottom ? "Bottom" : "Top", plain(p.rotationDeg),
                           numbers(part ? part->supplierNumbers : p.supplierNumbers),
                           part ? part->manufacturer : p.manufacturer } });
    }
    m_panel->showRows(std::move(rows));
    detail();
}

void JPlacerParts::detail() {
    if (!m_panel) return;
    const JPJob& job = m_job.job();
    if (m_chosen.size() != 1) {
        m_panel->showDetail(m_chosen.empty() ? "" : std::to_string(m_chosen.size()) + " placements chosen");
        return;
    }
    const JPPlacement* p = job.board.find(m_chosen.front());
    if (!p) {
        m_panel->showDetail("");
        return;
    }
    const JPPlacementState state = JPPlacementState::of(*p, job.parts);
    std::string text = p->designator + ": " + state.why;
    if (const JPPart* part = job.parts.part(p->partId)) {
        text += "\nPart: " + (part->mpn.empty() ? std::string("(no MPN)") : part->mpn);
        if (!part->manufacturer.empty()) text += ", " + part->manufacturer;
        if (!part->value.empty()) text += ", " + part->value;
        if (!part->supplierNumbers.empty()) text += "; " + numbers(part->supplierNumbers);
        text += part->origin.fromLibrary() ? " (from the library)" : " (new in this job)";
        if (const JPPackage* k = job.parts.package(part->packageId)) {
            text += "\nPackage: " + k->name;
            if (k->height > 0) text += ", height " + plain(k->height) + " mm";
            if (const JPFootprint* f = job.parts.footprint(k->footprintId))
                text += "\nFootprint: " + f->name + ", " + std::to_string(f->pads.size()) + " pads"
                      + (f->pin1.empty() ? ", pin 1 not named" : ", pin 1 is pad " + f->pin1);
        }
    }
    text += "\nThe file says: " + (p->footprint.empty() ? std::string("no footprint") : p->footprint);
    if (!p->supplierPackage.empty()) text += " (" + p->supplierPackage + ")";
    if (p->pins > 0) text += ", " + std::to_string(p->pins) + " pins";
    m_panel->showDetail(text);
}

} // inline namespace jf
