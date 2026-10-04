// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerRotations.h"

#include "library/JPPlacementRotation.h"
#include "library/JPRotationCheck.h"

#include <cstdio>

inline namespace jf {

namespace {

constexpr int kStatusMs = 8000;
using W = JPRotationCheck::Way;

const std::vector<std::string>& columns() {
    static const std::vector<std::string> c = { "Package", "Footprint", "Placements", "Settled", "Checked" };
    return c;
}

const std::vector<std::string>& onOff() {
    static const std::vector<std::string> v = { "On", "Off" };
    return v;
}

std::string deg(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g\xC2\xB0", JPPlacementRotation::normal(v));
    return buf;
}

std::vector<JPPartsPanel::Page> makePage(JSceneGraph& graph) {
    std::vector<JPPartsPanel::Page> pages;
    pages.push_back({ "Check", std::make_unique<JPFormPage>(graph,
        std::vector<JPFormPage::Field>{ { "rule", "Check rotations", true }, { "skip", "Skip where it cannot matter", true },
                                        { "file", "By the file's pad 1", true }, { "vision", "By vision", true },
                                        { "person", "By you, on the board", true }, { "turn", "Turn" } },
        std::vector<JPFormPage::Action>{ { "checkAll", "Check Rotations" }, { "fileTurn", "Set Turn from the File" },
                                         { "vision", "Check by Vision" }, { "board", "Check on Board\xE2\x80\xA6", true },
                                         { "ccw", "Turn 90\xC2\xB0 Anticlockwise" }, { "cw", "Turn 90\xC2\xB0 Clockwise" },
                                         { "accept", "Accept" }, { "cancel", "Cancel" }, { "uncheck", "Uncheck" } }) });
    return pages;
}

} // namespace

JPlacerRotations::JPlacerRotations(JPlacerJob& job, JPlacerMachine& machine, JSceneGraph& graph, JPlacerLayout& layout)
    : m_job(job), m_machine(machine), m_layout(layout) {
    m_panel = std::make_unique<JPPartsPanel>(graph, columns(), "packages", makePage(graph));
    m_panel->setView(JPPartsPanel::View::List, 3);
    m_panel->onChosen = [this](const std::vector<std::string>& keys) {
        m_chosen = keys.empty() ? std::string() : keys.front();
        page();
    };
    JPFormPage& p = m_panel->page(0);
    p.onAction = [this](const std::string& k) { onAction(k); };
    p.onChosen = [this](const std::string& k, int i) { onChoice(k, i); };
    m_dock = std::make_unique<JDockWidget>(kTitle, 0.f, 0.f, 0.f, 0.f);
    m_dock->setContent(m_panel.get());
    m_layout.add(m_dock.get(), JPlacerLayout::Home::Work);
    m_watch = m_job.watch([this](JPlacerJob::Change what) {
        if (what == JPlacerJob::Change::Board) {
            m_notes.clear();
            m_fileSays.clear();
            m_visionQueue.clear();
            cancel();
        }
        show();
    });
    show();
}

JPlacerRotations::~JPlacerRotations() {
    *m_alive = false;
    m_job.unwatch(m_watch);
    if (JPlacerBoard* b = m_machine.board()) b->clearFootprint();
    m_layout.remove(m_dock.get());
    m_dock->setContent(nullptr);
}

void JPlacerRotations::showDock() {
    m_layout.show(m_dock.get());
}

void JPlacerRotations::edited() {
    m_job.changed(JPlacerJob::Change::Parts);
}

std::map<std::string, std::vector<const JPPlacement*>> JPlacerRotations::packages() const {
    std::map<std::string, std::vector<const JPPlacement*>> out;
    const JPJob& j = m_job.job();
    for (const JPPlacement& p : j.board.placements) {
        if (p.fiducial || p.doNotPlace) continue;
        const JPPart* part = j.parts.part(p.partId);
        if (part && j.parts.package(part->packageId)) out[part->packageId].push_back(&p);
    }
    return out;
}

void JPlacerRotations::show() {
    const JPJob& j = m_job.job();
    std::vector<JPPartsPanel::Row> rows;
    for (const auto& [id, places] : packages()) {
        const JPPackage& k = *j.parts.package(id);
        const JPFootprint* f = j.parts.footprint(k.footprintId);
        std::string checked = JPRotationCheck::checked(k) ? k.checkedBy == "person" ? "by you" : k.checkedBy
                                                          : j.rotationRules.check ? "to check" : "(the rule is off)";
        rows.push_back({ id, { k.name, f ? f->name : "(none)", std::to_string(places.size()),
                               f ? JPRotationCheck::name(JPRotationCheck::way(*f, places)) : "needs a footprint", checked } });
    }
    m_panel->showRows(std::move(rows));
    page();
}

void JPlacerRotations::page() {
    JPFormPage& p = m_panel->page(0);
    const JPJob& j = m_job.job();
    const JPRotationRules& r = j.rotationRules;
    p.setChoices("rule", onOff(), r.check ? 0 : 1);
    p.setChoices("skip", onOff(), r.skipCannotMatter ? 0 : 1);
    p.setChoices("file", onOff(), r.byFile ? 0 : 1);
    p.setChoices("vision", onOff(), r.byVision ? 0 : 1);
    p.setChoices("person", onOff(), r.byPerson ? 0 : 1);
    p.setFieldEditable("turn", false);
    const auto all = packages();
    int toCheck = 0;
    for (const auto& [id, places] : all) toCheck += JPRotationCheck::checked(*j.parts.package(id)) ? 0 : 1;
    const auto it = all.find(m_chosen);
    const JPPackage* k = it != all.end() ? j.parts.package(m_chosen) : nullptr;
    const JPFootprint* f = k ? j.parts.footprint(k->footprintId) : nullptr;
    const bool checking = !m_checking.empty();
    JPBoardSide located;
    const bool onMachine = m_machine.board() && m_machine.board()->located(located);
    p.setActionEnabled("checkAll", r.check && !checking);
    p.setActionEnabled("fileTurn", k && m_fileSays.count(m_chosen) && !checking);
    p.setActionEnabled("vision", f && onMachine && !checking);
    p.setActionEnabled("board", f && onMachine && r.byPerson && !checking);
    for (const char* a : { "ccw", "cw", "accept", "cancel" }) p.setActionEnabled(a, checking);
    p.setActionEnabled("uncheck", k && JPRotationCheck::checked(*k) && !checking);
    p.setValue("turn", checking ? deg(m_pendingTurn) + " (trying)" : k ? deg(k->turnDeg) : "");
    std::string note = r.check ? "Rotations: " + std::to_string(toCheck) + " of " + std::to_string(all.size()) + " package(s) to check."
                               : "The rotation check is off for this job.";
    if (k) {
        note += " " + k->name + ": " + (f ? std::string("settled ") + JPRotationCheck::name(JPRotationCheck::way(*f, it->second))
                                          : std::string("needs a footprint first"));
        if (JPRotationCheck::checked(*k)) note += "; checked " + (k->checkedBy == "person" ? std::string("by you") : k->checkedBy);
        if (const auto n = m_notes.find(m_chosen); n != m_notes.end()) note += ". " + n->second;
    }
    if (checking)
        note = "On the board: turn the drawn footprint until its pin-1 dot sits on the board's pin-1 mark, then Accept.";
    else if (!onMachine)
        note += " (Vision and the board check need the board located on the machine.)";
    p.setNote(note);
}

void JPlacerRotations::onChoice(const std::string& key, int index) {
    JPRotationRules& r = m_job.job().rotationRules;
    const bool on = index == 0;
    if (key == "rule") r.check = on;
    else if (key == "skip") r.skipCannotMatter = on;
    else if (key == "file") r.byFile = on;
    else if (key == "vision") r.byVision = on;
    else if (key == "person") r.byPerson = on;
    edited();
}

void JPlacerRotations::checkAll() {
    JPJob& j = m_job.job();
    const JPRotationRules& r = j.rotationRules;
    if (!r.check) return;
    JPBoardSide located;
    const bool onMachine = m_machine.board() && m_machine.board()->located(located);
    m_visionQueue.clear();
    int settled = 0, waiting = 0;
    for (const auto& [id, places] : packages()) {
        JPPackage* k = j.parts.package(id);
        if (JPRotationCheck::checked(*k)) continue;
        const JPFootprint* f = j.parts.footprint(k->footprintId);
        if (!f) {
            m_notes[id] = "It needs a footprint before it can be checked.";
            continue;
        }
        const W way = JPRotationCheck::way(*f, places);
        // Each way allowed, cheapest first; one not allowed falls to the next.
        if (way == W::CannotMatter && r.skipCannotMatter) {
            JPRotationCheck::markChecked(*k, "cannot matter");
            ++settled;
            continue;
        }
        if (r.byFile && f->pin1Pad()) {
            const JPRotationCheck::FileVerdict v = JPRotationCheck::byFile(*f, k->turnDeg, places);
            if (v.compared > 0 && v.uniform && v.quarters == 0) {
                JPRotationCheck::markChecked(*k, "file");
                ++settled;
                continue;
            }
            if (v.compared > 0 && v.uniform) {
                m_fileSays[id] = v.quarters;
                m_notes[id] = "The file's pad 1 says it is turned " + std::to_string(v.quarters * 90)
                            + "\xC2\xB0 from the package's 0\xC2\xB0: Set Turn from the File.";
                continue;
            }
            if (v.compared > 0) {
                std::string odd;
                for (const std::string& d : v.odd) odd += " " + d;
                m_notes[id] = "The file's pad 1 does not agree from placement to placement (these differ:" + odd
                            + "): turned on purpose in the CAD, or the file is wrong. Nothing was set.";
                continue;
            }
        }
        if (r.byVision && !JPRotationCheck::symmetric(*f, 1) && !JPRotationCheck::symmetric(*f, 2)) {
            if (onMachine) {
                m_visionQueue.push_back(id);
                ++waiting;
            } else {
                m_notes[id] = "Vision can settle it once the board is located on the machine.";
            }
            continue;
        }
        m_notes[id] = r.byPerson ? "Only you can settle it: Check on Board." : "Not checked: the ways that could settle it are off.";
    }
    if (settled > 0) edited();
    m_job.window().showStatus("Rotations: " + std::to_string(settled) + " settled now" +
                                  (waiting ? ", " + std::to_string(waiting) + " for the camera" : ""),
                              kStatusMs);
    show();
    nextVision();
}

void JPlacerRotations::onlyVision(const std::string& packageId) {
    m_visionQueue = { packageId };
    nextVision();
}

void JPlacerRotations::nextVision() {
    if (m_visionQueue.empty()) return;
    JPlacerBoard* board = m_machine.board();
    JPlacerCameraTasks* tasks = m_machine.cameraTasks();
    JPBoardSide located;
    if (!board || !tasks || !board->located(located)) {
        m_visionQueue.clear();
        return;
    }
    const std::string id = m_visionQueue.front();
    m_visionQueue.pop_front();
    const auto all = packages();
    const auto it = all.find(id);
    const JPPackage* k = m_job.job().parts.package(id);
    const JPFootprint* f = k ? m_job.job().parts.footprint(k->footprintId) : nullptr;
    if (it == all.end() || !f) {
        nextVision();
        return;
    }
    // The first of its placements on the side that is up.
    const JPPlacement* at = nullptr;
    for (const JPPlacement* p : it->second)
        if (p->side == located.side) {
            at = p;
            break;
        }
    if (!at) {
        m_notes[id] = "None of its placements is on the side that is up.";
        show();
        nextVision();
        return;
    }
    const JPPlacement place = *at;
    const JPFootprint footprint = *f;
    std::weak_ptr<bool> alive = m_alive;
    tasks->checkRotation(located, place, footprint, place.rotationDeg + k->turnDeg, [this, alive, id](const JPRotationLook::Result& r) {
        if (const auto a = alive.lock(); !a || !*a) return;
        JPPackage* k = m_job.job().parts.package(id);
        if (k && r.ok) {
            k->turnDeg = JPPlacementRotation::normal(k->turnDeg + 90 * r.quarters);
            ++k->revision;
            JPRotationCheck::markChecked(*k, "vision");
            m_notes[id] = r.quarters == 0 ? "Vision found it as the package has it."
                                          : "Vision found it turned " + std::to_string(r.quarters * 90) + "\xC2\xB0: the turn was set.";
            edited();
        } else if (k) {
            m_notes[id] = r.why;
            show();
        }
        nextVision();
    });
}

void JPlacerRotations::onBoard(int quarters) {
    JPlacerBoard* board = m_machine.board();
    JPBoardSide located;
    if (!board || !board->located(located)) return;
    const JPJob& j = m_job.job();
    if (quarters == 0) {
        const auto all = packages();
        const auto it = all.find(m_chosen);
        const JPPackage* k = it != all.end() ? j.parts.package(m_chosen) : nullptr;
        if (!k) return;
        m_checkingAt.clear();
        for (const JPPlacement* p : it->second)
            if (p->side == located.side) {
                m_checkingAt = p->designator;
                break;
            }
        if (m_checkingAt.empty()) {
            m_job.window().showStatus("None of " + k->name + "'s placements is on the side that is up", kStatusMs);
            return;
        }
        m_checking = m_chosen;
        m_pendingTurn = k->turnDeg;
    } else {
        m_pendingTurn = JPPlacementRotation::normal(m_pendingTurn + 90 * quarters);
    }
    const JPPackage* k = j.parts.package(m_checking);
    const JPFootprint* f = k ? j.parts.footprint(k->footprintId) : nullptr;
    const JPPlacement* p = j.board.find(m_checkingAt);
    if (!f || !p) return cancel();
    board->showFootprint(m_checkingAt, *f, p->rotationDeg + m_pendingTurn);
    page();
}

void JPlacerRotations::accept() {
    JPPackage* k = m_job.job().parts.package(m_checking);
    if (k) {
        k->turnDeg = m_pendingTurn;
        ++k->revision;
        JPRotationCheck::markChecked(*k, "person");
        m_notes[m_checking] = "Checked on the board.";
    }
    cancel();
    edited();
}

void JPlacerRotations::cancel() {
    m_checking.clear();
    m_checkingAt.clear();
    if (JPlacerBoard* b = m_machine.board()) b->clearFootprint();
    page();
}

void JPlacerRotations::onAction(const std::string& key) {
    if (key == "checkAll") checkAll();
    else if (key == "vision") onlyVision(m_chosen);
    else if (key == "board") onBoard(0);
    else if (key == "ccw") onBoard(1);
    else if (key == "cw") onBoard(-1);
    else if (key == "accept") accept();
    else if (key == "cancel") cancel();
    else if (key == "fileTurn") {
        JPPackage* k = m_job.job().parts.package(m_chosen);
        const auto it = m_fileSays.find(m_chosen);
        if (!k || it == m_fileSays.end()) return;
        k->turnDeg = JPPlacementRotation::normal(k->turnDeg + 90 * it->second);
        ++k->revision;
        JPRotationCheck::markChecked(*k, "file");
        m_fileSays.erase(it);
        m_notes[m_chosen] = "The turn was set from the file's pad 1.";
        edited();
    } else if (key == "uncheck") {
        if (JPPackage* k = m_job.job().parts.package(m_chosen)) {
            k->checkedBy.clear();
            edited();
        }
    }
}

} // inline namespace jf
