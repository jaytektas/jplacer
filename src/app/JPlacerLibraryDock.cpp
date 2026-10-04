// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerLibraryDock.h"

#include "JPlacerEntryForm.h"

#include "library/JPEntryFields.h"
#include "library/JPKicadFootprint.h"
#include "library/JPLibrarySync.h"

#include <j/core/Dialog.h>

#include <cstdio>

inline namespace jf {

namespace {

using K = JPEntry::Kind;

constexpr size_t kPartPage = 0, kPackagePage = 1, kFootprintPage = 2;

const std::vector<std::string>& columns() {
    static const std::vector<std::string> c = { "Name", "Kind", "Value", "Package", "Footprint", "Pads", "Height", "Names" };
    return c;
}

std::string plain(double v) {
    if (v <= 0) return {};
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

size_t pageOf(K kind) {
    return kind == K::Part ? kPartPage : kind == K::Package ? kPackagePage : kFootprintPage;
}

std::vector<JPFormPage::Action> actions(K kind) {
    std::vector<JPFormPage::Action> a = { { "bring", "Bring into Job" } };
    if (kind == K::Part) a.push_back({ "newPart", "New Part" });
    if (kind == K::Package)
        a.insert(a.end(), { { "newPackage", "New Package" }, { "importKicad", "Import KiCad Footprint\xE2\x80\xA6" },
                            { "makeDual", "Make Dual Footprint\xE2\x80\xA6" }, { "makeQuad", "Make Quad Footprint\xE2\x80\xA6" } });
    a.push_back({ "remove", "Remove\xE2\x80\xA6" });
    return a;
}

} // namespace

JPlacerLibraryDock::JPlacerLibraryDock(JPlacerJob& job, JSceneGraph& graph, JPlacerLayout& layout)
    : m_job(job), m_layout(layout) {
    std::vector<JPPartsPanel::Page> pageList;
    pageList.push_back({ "Part", std::make_unique<JPFormPage>(graph, JPlacerEntryForm::fields(K::Part), actions(K::Part)) });
    pageList.push_back({ "Package", std::make_unique<JPFormPage>(graph, JPlacerEntryForm::fields(K::Package), actions(K::Package)) });
    auto picture = std::make_unique<JPFootprintView>(graph);
    m_footprintView = picture.get();
    pageList.push_back({ "Footprint", std::make_unique<JPFormPage>(graph, JPlacerEntryForm::fields(K::Footprint),
                                                                actions(K::Footprint), std::move(picture)) });
    m_panel = std::make_unique<JPPartsPanel>(graph, columns(), "entries", std::move(pageList));
    m_panel->setView(JPPartsPanel::View::List, 1);
    m_panel->onChosen = [this](const std::vector<std::string>& keys) {
        m_chosen = keys.empty() ? std::string() : keys.front();
        pages();
        const JPEntry e = chosenEntry();
        if (!e.id.empty()) m_panel->showPage(pageOf(e.kind));
    };
    for (const size_t p : { kPartPage, kPackagePage, kFootprintPage }) {
        const K kind = p == kPartPage ? K::Part : p == kPackagePage ? K::Package : K::Footprint;
        JPFormPage& page = m_panel->page(p);
        // A page acts on the chosen entry when it is of its kind; New Part and
        // New Package act whatever is chosen.
        auto entry = [this, kind] {
            const JPEntry e = chosenEntry();
            return e.kind == kind ? e : JPEntry { kind, {} };
        };
        page.onField = [this, entry](const std::string& k, const std::string& t) { onField(entry(), k, t); };
        page.onChosen = [this, entry](const std::string& k, int i) { onChoice(entry(), k, i); };
        page.onAction = [this, entry](const std::string& k) { onAction(entry(), k); };
    }
    m_dock = std::make_unique<JDockWidget>(kTitle, 0.f, 0.f, 0.f, 0.f);
    m_dock->setContent(m_panel.get());
    m_layout.add(m_dock.get(), JPlacerLayout::Home::Work);
    m_watch = m_job.watch([this](JPlacerJob::Change what) {
        if (what == JPlacerJob::Change::Library) show();
        else pages();   // Bring into Job's state follows the job
    });
    show();
}

JPlacerLibraryDock::~JPlacerLibraryDock() {
    *m_alive = false;
    m_job.unwatch(m_watch);
    m_layout.remove(m_dock.get());
    m_dock->setContent(nullptr);
}

void JPlacerLibraryDock::showDock() {
    m_layout.show(m_dock.get());
}

void JPlacerLibraryDock::say(const std::string& text) {
    JDialog::message("Library", text);
}

void JPlacerLibraryDock::changed() {
    m_job.libraryChanged();
}

JPEntry JPlacerLibraryDock::chosenEntry() const {
    const size_t colon = m_chosen.find(':');
    if (colon == std::string::npos) return { K::Part, {} };
    const std::string kind = m_chosen.substr(0, colon), id = m_chosen.substr(colon + 1);
    const JPPartsStore& s = m_job.library().store();
    if (kind == "part" && s.part(id)) return { K::Part, id };
    if (kind == "package" && s.package(id)) return { K::Package, id };
    if (kind == "footprint" && s.footprint(id)) return { K::Footprint, id };
    return { K::Part, {} };
}

void JPlacerLibraryDock::show() {
    const JPPartsStore& s = m_job.library().store();
    std::vector<JPPartsPanel::Row> rows;
    auto names = [](const std::vector<std::string>& v) {
        std::string out;
        for (const std::string& n : v) out += (out.empty() ? "" : ", ") + n;
        return out;
    };
    for (const JPPart& p : s.parts) {
        const JPPackage* k = s.package(p.packageId);
        const JPFootprint* f = k ? s.footprint(k->footprintId) : nullptr;
        rows.push_back({ "part:" + p.id, { p.label(), "Part", p.value, k ? k->name : "", f ? f->name : "",
                                           f ? std::to_string(f->pads.size()) : "", k ? plain(k->height) : "", "" } });
    }
    for (const JPPackage& k : s.packages) {
        const JPFootprint* f = s.footprint(k.footprintId);
        rows.push_back({ "package:" + k.id, { k.name, "Package", "", k.name, f ? f->name : "",
                                              f ? std::to_string(f->pads.size()) : "", plain(k.height), names(k.names) } });
    }
    for (const JPFootprint& f : s.footprints)
        rows.push_back({ "footprint:" + f.id, { f.name, "Footprint", "", "", f.name, std::to_string(f.pads.size()), "", "" } });
    m_panel->showRows(std::move(rows));
    pages();
}

void JPlacerLibraryDock::pages() {
    const JPPartsStore& s = m_job.library().store();
    const bool writable = !m_job.library().readOnly();
    const JPEntry chosen = chosenEntry();
    for (const size_t p : { kPartPage, kPackagePage, kFootprintPage }) {
        const K kind = p == kPartPage ? K::Part : p == kPackagePage ? K::Package : K::Footprint;
        JPFormPage& page = m_panel->page(p);
        const JPEntry e = chosen.kind == kind ? chosen : JPEntry { kind, {} };
        const bool there = !e.id.empty();
        JPlacerEntryForm::fill(page, s, e);
        page.setEditable(there && writable);
        page.setFieldEditable("pads", false);
        page.setActionEnabled("bring", there);
        page.setActionEnabled("remove", there && writable);
        page.setActionEnabled("newPart", writable);
        page.setActionEnabled("newPackage", writable);
        for (const char* a : { "importKicad", "makeDual", "makeQuad" }) page.setActionEnabled(a, there && writable);
        auto choices = [&](const char* key, K what, const std::string& current, std::vector<std::string>& ids) {
            ids = { std::string() };
            std::vector<std::string> labels = { "(none)" };
            int at = 0;
            if (what == K::Package)
                for (const JPPackage& k : s.packages) {
                    if (k.id == current) at = int(ids.size());
                    ids.push_back(k.id);
                    labels.push_back(k.name);
                }
            else
                for (const JPFootprint& f : s.footprints) {
                    if (f.id == current) at = int(ids.size());
                    ids.push_back(f.id);
                    labels.push_back(f.name);
                }
            page.setChoices(key, labels, at);
        };
        if (kind == K::Part) choices("package", K::Package, there ? s.part(e.id)->packageId : "", m_packageChoices);
        if (kind == K::Package) choices("footprint", K::Footprint, there ? s.package(e.id)->footprintId : "", m_footprintChoices);
        if (kind == K::Footprint) m_footprintView->show(there ? std::optional<JPFootprint>(*s.footprint(e.id)) : std::nullopt);
        std::string note;
        if (!writable) note = m_job.library().problem();
        else if (!there) note = std::string("Choose a ") + JPEntry::name(kind) + " in the list.";
        else if (kind == K::Part) note = "Edits are saved to the library as you make them; jobs that copied it are told it has changed.";
        else if (kind == K::Package) note = "Names: the CAD footprint and supplier package names that find this package at import.";
        page.setNote(note);
    }
}

void JPlacerLibraryDock::onField(const JPEntry& e, const std::string& key, const std::string& text) {
    if (e.id.empty() || m_job.library().readOnly()) return;
    std::string error;
    if (!JPEntryFields::set(m_job.library().store(), e, key, text, error)) {
        say(error.empty() ? "That cannot be changed here." : error);
        pages();
        return;
    }
    changed();
}

void JPlacerLibraryDock::onChoice(const JPEntry& e, const std::string& key, int index) {
    JPPartsStore& s = m_job.library().store();
    if (e.id.empty() || m_job.library().readOnly()) return;
    const std::vector<std::string>& ids = key == "package" ? m_packageChoices : m_footprintChoices;
    if (index < 0 || size_t(index) >= ids.size()) return;
    if (key == "package" && e.kind == K::Part) {
        s.part(e.id)->packageId = ids[size_t(index)];
        ++s.part(e.id)->revision;
    } else if (key == "footprint" && e.kind == K::Package) {
        s.package(e.id)->footprintId = ids[size_t(index)];
        ++s.package(e.id)->revision;
    }
    changed();
}

void JPlacerLibraryDock::onAction(const JPEntry& e, const std::string& key) {
    JPPartsStore& s = m_job.library().store();
    std::weak_ptr<bool> alive = m_alive;
    if (key == "bring") {
        if (e.id.empty()) return;
        JPLibrarySync::bringIntoJob(m_job.job().parts, e, s);
        m_job.changed(JPlacerJob::Change::Parts);
        m_job.window().showStatus(std::string("The ") + JPEntry::name(e.kind) + " is in the job now: choose it on the Parts dock", 6000);
        return;
    }
    if (m_job.library().readOnly()) return;
    if (key == "newPart") {
        JPPart p;
        p.value = "new part";
        m_chosen = "part:" + s.add(std::move(p));
        changed();
    } else if (key == "newPackage") {
        JPPackage k;
        k.name = "new package";
        m_chosen = "package:" + s.add(std::move(k));
        changed();
    } else if (key == "remove" && !e.id.empty()) {
        JDialogOptions opts;
        opts.okLabel = "Remove";
        JDialog::confirm(std::string("Remove from the Library?"),
                         std::string("Remove this ") + JPEntry::name(e.kind) + " from the library? Jobs keep their own copies.",
                         [this, alive, e] {
                             if (const auto a = alive.lock(); !a || !*a) return;
                             m_job.library().store().remove(e);
                             m_chosen.clear();
                             changed();
                         },
                         {}, opts);
    } else if (key == "importKicad" && !e.id.empty()) {
        JDialog::openFile("Import KiCad Footprint", { "kicad_mod" }, [this, alive, e](std::string path) {
            if (const auto a = alive.lock(); !a || !*a) return;
            JPFootprint f;
            std::string error;
            if (!JPKicadFootprint::read(path, f, error)) {
                say(error);
                return;
            }
            JPPartsStore& store = m_job.library().store();
            const std::string id = store.add(std::move(f));
            if (JPPackage* k = store.package(e.id)) {
                k->footprintId = id;
                ++k->revision;
            }
            changed();
        });
    } else if ((key == "makeDual" || key == "makeQuad") && !e.id.empty()) {
        const bool quad = key == "makeQuad";
        JDialog::input(quad ? "Make Quad Footprint" : "Make Dual Footprint", JPlacerEntryForm::prompt(quad),
            [this, alive, e, quad](std::string text) {
                if (const auto a = alive.lock(); !a || !*a) return;
                JPPartsStore& store = m_job.library().store();
                const JPPackage* k = store.package(e.id);
                if (!k) return;
                JPFootprint f;
                std::string error;
                if (!JPlacerEntryForm::make(quad, text, k->name, f, error)) {
                    say(error);
                    return;
                }
                const std::string id = store.add(std::move(f));
                store.package(e.id)->footprintId = id;
                ++store.package(e.id)->revision;
                changed();
            },
            {}, quad ? "16, 0.5, 4.95, 0.85, 0.25, 3.45" : "8, 1.27, 4.95, 1.95, 0.6");
    }
}

} // inline namespace jf
