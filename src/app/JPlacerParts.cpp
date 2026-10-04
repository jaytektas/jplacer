// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerParts.h"

#include "JPlacerEntryForm.h"
#include "JPlacerSettings.h"

#include "library/JPEntryFields.h"
#include "library/JPKicadFootprint.h"
#include "library/JPLibrarySync.h"
#include "library/JPPartMatcher.h"
#include "library/JPPlacementRotation.h"
#include "library/JPPlacementState.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>

#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

using K = JPEntry::Kind;

constexpr size_t kPlacementPage = 0, kPartPage = 1, kPackagePage = 2, kFootprintPage = 3;
constexpr const char* kLibraryPrefix = "lib:";

const std::vector<std::string>& columns() {
    static const std::vector<std::string> c = { "Designator", "State", "Part", "Value", "Package", "Footprint", "Side",
                                                "Rotation", "Rotation From", "Supplier No.", "Manufacturer" };
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

std::vector<JPFormPage::Action> entryActions(K kind) {
    std::vector<JPFormPage::Action> a;
    if (kind == K::Package)
        a = { { "importKicad", "Import KiCad Footprint\xE2\x80\xA6" }, { "makeDual", "Make Dual Footprint\xE2\x80\xA6" },
              { "makeQuad", "Make Quad Footprint\xE2\x80\xA6" } };
    a.insert(a.end(), { { "copy", "Copy to Library" }, { "update", "Update from Library" },
                        { "keep", "Keep This Version" }, { "remove", "Remove\xE2\x80\xA6" } });
    return a;
}

std::vector<JPPartsPanel::Page> makePages(JSceneGraph& graph, JPFootprintView*& view) {
    std::vector<JPPartsPanel::Page> pages;
    pages.push_back({ "Placement", std::make_unique<JPFormPage>(graph,
        std::vector<JPFormPage::Field>{ { "designator", "Placement" }, { "rotation", "Rotation (\xC2\xB0)" },
                                        { "from", "Rotation from" }, { "part", "Part", true } },
        std::vector<JPFormPage::Action>{ { "partRotation", "Use Part's Rotation" }, { "confirm", "Confirm Part" },
                                         { "newPart", "New Part from File" } }) });
    pages.push_back({ "Part", std::make_unique<JPFormPage>(graph, JPlacerEntryForm::fields(K::Part), entryActions(K::Part)) });
    pages.push_back({ "Package", std::make_unique<JPFormPage>(graph, JPlacerEntryForm::fields(K::Package), entryActions(K::Package)) });
    auto picture = std::make_unique<JPFootprintView>(graph);
    view = picture.get();
    pages.push_back({ "Footprint", std::make_unique<JPFormPage>(graph, JPlacerEntryForm::fields(K::Footprint),
                                                                entryActions(K::Footprint), std::move(picture)) });
    return pages;
}

} // namespace

JPlacerParts::JPlacerParts(JPlacerJob& job, JSceneGraph& graph, JPlacerLayout& layout)
    : m_job(job), m_layout(layout) {
    m_panel = std::make_unique<JPPartsPanel>(graph, columns(), "placements", makePages(graph, m_footprintView));
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
        pages();
    };
    JPFormPage& placement = m_panel->page(kPlacementPage);
    placement.onField = [this](const std::string& k, const std::string& t) { onPlacementField(k, t); };
    placement.onChosen = [this](const std::string& k, int i) { onPlacementChoice(k, i); };
    placement.onAction = [this](const std::string& k) { onPlacementAction(k); };
    for (const size_t p : { kPartPage, kPackagePage, kFootprintPage }) {
        JPFormPage& page = m_panel->page(p);
        page.onField = [this, p](const std::string& k, const std::string& t) { onEntryField(p, k, t); };
        page.onChosen = [this, p](const std::string& k, int i) { onEntryChoice(p, k, i); };
        page.onAction = [this, p](const std::string& k) { onEntryAction(p, k); };
    }
    m_dock = std::make_unique<JDockWidget>(kTitle, 0.f, 0.f, 0.f, 0.f);
    m_dock->setContent(m_panel.get());
    m_layout.add(m_dock.get(), JPlacerLayout::Home::Work);
    m_watch = m_job.watch([this](JPlacerJob::Change what) {
        if (what == JPlacerJob::Change::Board) m_chosen.clear();
        show();
    });
    show();
}

JPlacerParts::~JPlacerParts() {
    *m_alive = false;
    m_job.unwatch(m_watch);
    m_layout.remove(m_dock.get());
    m_dock->setContent(nullptr);
}

void JPlacerParts::showDock() {
    m_layout.show(m_dock.get());
}

void JPlacerParts::say(const std::string& text) {
    JDialog::message("Parts", text);
}

void JPlacerParts::edited(JPlacerJob::Change what) {
    m_job.changed(what);
}

void JPlacerParts::show() {
    const JPJob& job = m_job.job();
    std::vector<JPPartsPanel::Row> rows;
    for (const JPPlacement& p : job.board.placements) {
        const JPPlacementState state = JPPlacementState::of(p, job.parts);
        const JPPlacementRotation rot = JPPlacementRotation::of(p, job.parts);
        const JPPart* part = job.parts.part(p.partId);
        const JPPackage* package = part ? job.parts.package(part->packageId) : nullptr;
        const JPFootprint* footprint = package ? job.parts.footprint(package->footprintId) : nullptr;
        rows.push_back({ p.designator,
                         { p.designator, JPPlacementState::name(state.kind), part ? part->label() : std::string(),
                           part ? part->value : p.value, package ? package->name : std::string(),
                           footprint ? footprint->name : p.footprint,
                           p.side == JPPlacement::Side::Bottom ? "Bottom" : "Top", plain(rot.degrees),
                           p.fiducial ? std::string() : JPPlacementRotation::name(rot.source),
                           numbers(part ? part->supplierNumbers : p.supplierNumbers),
                           part ? part->manufacturer : p.manufacturer } });
    }
    m_panel->showRows(std::move(rows));
    pages();
}

std::vector<JPPlacement*> JPlacerParts::chosen() {
    std::vector<JPPlacement*> out;
    for (JPPlacement& p : m_job.job().board.placements)
        for (const std::string& k : m_chosen)
            if (p.designator == k) out.push_back(&p);
    return out;
}

JPEntry JPlacerParts::entryOn(size_t page) {
    const std::vector<JPPlacement*> ps = chosen();
    const JPPartsStore& s = m_job.job().parts;
    const JPPart* part = ps.empty() ? nullptr : s.part(ps.front()->partId);
    if (page == kPartPage) return { K::Part, part ? part->id : std::string() };
    const JPPackage* package = part ? s.package(part->packageId) : nullptr;
    if (page == kPackagePage) return { K::Package, package ? package->id : std::string() };
    const JPFootprint* footprint = package ? s.footprint(package->footprintId) : nullptr;
    return { K::Footprint, footprint ? footprint->id : std::string() };
}

void JPlacerParts::pages() {
    placementPage();
    for (const size_t p : { kPartPage, kPackagePage, kFootprintPage }) entryPage(p, entryOn(p));
}

void JPlacerParts::placementPage() {
    JPFormPage& page = m_panel->page(kPlacementPage);
    const std::vector<JPPlacement*> ps = chosen();
    const JPPartsStore& s = m_job.job().parts;
    page.setEditable(!ps.empty());
    for (const char* a : { "partRotation", "confirm", "newPart" }) page.setActionEnabled(a, !ps.empty());
    page.setFieldEditable("designator", false);
    page.setFieldEditable("from", false);
    m_partChoices = { std::string() };
    std::vector<std::string> labels = { "(none)" };
    for (const JPPart& p : s.parts) {
        m_partChoices.push_back(p.id);
        const JPPackage* k = s.package(p.packageId);
        labels.push_back(p.label() + (k ? "  \xE2\x80\x94  " + k->name : std::string()));
    }
    if (ps.empty()) {
        page.setNote("Choose a placement in the list (Shift or Ctrl to choose several).");
        for (const char* f : { "designator", "rotation", "from" }) page.setValue(f, "");
        page.setChoices("part", labels, 0);
        return;
    }
    const JPPlacement& first = *ps.front();
    const JPPlacementRotation rot = JPPlacementRotation::of(first, s);
    int chosenPart = 0;
    for (size_t i = 0; i < m_partChoices.size(); ++i)
        if (m_partChoices[i] == first.partId) chosenPart = int(i);
    page.setChoices("part", labels, chosenPart);
    if (ps.size() == 1) {
        page.setNote(first.designator + ": " + JPPlacementState::of(first, s).why + ". The file says "
                     + (first.footprint.empty() ? std::string("no footprint") : first.footprint)
                     + (first.pins > 0 ? ", " + std::to_string(first.pins) + " pins" : std::string())
                     + "; rotation " + plain(first.rotationDeg) + "\xC2\xB0.");
        page.setValue("designator", first.designator);
    } else {
        page.setNote(std::to_string(ps.size()) + " placements chosen: a rotation or part set here is set on every one of them.");
        page.setValue("designator", first.designator + " and " + std::to_string(ps.size() - 1) + " more");
    }
    page.setValue("rotation", plain(rot.degrees));
    page.setValue("from", JPPlacementRotation::name(rot.source));
}

void JPlacerParts::entryPage(size_t pageIndex, const JPEntry& e) {
    JPFormPage& page = m_panel->page(pageIndex);
    const JPPartsStore& s = m_job.job().parts;
    const JPPartsStore& lib = m_job.library().store();
    const bool there = !e.id.empty();
    JPlacerEntryForm::fill(page, s, e);
    page.setEditable(there);
    page.setFieldEditable("pads", false);
    const bool newer = there && JPLibrarySync::libraryNewer(s, e, lib);
    page.setActionEnabled("copy", there && !m_job.library().readOnly());
    page.setActionEnabled("update", newer);
    page.setActionEnabled("keep", newer);
    page.setActionEnabled("remove", there);
    for (const char* a : { "importKicad", "makeDual", "makeQuad" }) page.setActionEnabled(a, there);

    // The choice of a part's package, or a package's footprint: the job's, or one brought from the library.
    auto choices = [&](const char* key, K kind, const std::string& current, std::vector<std::string>& ids) {
        ids = { std::string() };
        std::vector<std::string> labels = { "(none)" };
        int at = 0;
        auto offer = [&](const std::string& id, const std::string& name, bool library) {
            if (!library && id == current) at = int(ids.size());
            ids.push_back(library ? kLibraryPrefix + id : id);
            labels.push_back(library ? "Library: " + name : name);
        };
        if (kind == K::Package) {
            for (const JPPackage& k : s.packages) offer(k.id, k.name, false);
            for (const JPPackage& k : lib.packages)
                if (!s.packageFrom(k.id)) offer(k.id, k.name, true);
        } else {
            for (const JPFootprint& f : s.footprints) offer(f.id, f.name, false);
            for (const JPFootprint& f : lib.footprints)
                if (!s.footprintFrom(f.id)) offer(f.id, f.name, true);
        }
        page.setChoices(key, labels, at);
    };
    if (e.kind == K::Part) choices("package", K::Package, there ? s.part(e.id)->packageId : std::string(), m_packageChoices);
    if (e.kind == K::Package) choices("footprint", K::Footprint, there ? s.package(e.id)->footprintId : std::string(), m_footprintChoices);
    if (e.kind == K::Footprint) m_footprintView->show(there ? std::optional<JPFootprint>(*s.footprint(e.id)) : std::nullopt);

    if (!there) {
        page.setNote(m_chosen.empty() ? "Choose a placement in the list." : std::string("The chosen placement has no ")
                                                                             + JPEntry::name(e.kind) + ".");
        return;
    }
    std::string note;
    const JPOrigin* o = e.kind == K::Part ? &s.part(e.id)->origin : e.kind == K::Package ? &s.package(e.id)->origin : &s.footprint(e.id)->origin;
    if (o->fromLibrary()) note = "From the library";
    else note = JPLibrarySync::counterpart(s, e, lib).id.empty() ? "New in this job: not in the library"
                                                                 : "Made in this job; the library has one of the same name";
    if (JPLibrarySync::changedInJob(s, e)) note += "; changed in this job";
    if (newer) note += ". The library has a newer version: Update from Library takes it, Keep This Version keeps this one";
    if (e.kind == K::Part && chosen().size() > 1) note += ". (The part of " + chosen().front()->designator + ".)";
    page.setNote(note + ".");
}

void JPlacerParts::onPlacementField(const std::string& key, const std::string& text) {
    if (key != "rotation") return;
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
        say("'" + text + "' is not an angle in degrees.");
        show();
        return;
    }
    for (JPPlacement* p : chosen()) {
        p->rotationSet = true;
        p->rotationSetDeg = JPPlacementRotation::normal(v);
    }
    edited();
}

void JPlacerParts::onPlacementChoice(const std::string& key, int index) {
    if (key != "part" || index < 0 || size_t(index) >= m_partChoices.size()) return;
    for (JPPlacement* p : chosen()) {
        p->partId = m_partChoices[size_t(index)];
        p->partGuessed = false;
    }
    edited();
}

void JPlacerParts::onPlacementAction(const std::string& key) {
    const std::vector<JPPlacement*> ps = chosen();
    if (ps.empty()) return;
    if (key == "partRotation") {
        for (JPPlacement* p : ps) p->rotationSet = false;
    } else if (key == "confirm") {
        for (JPPlacement* p : ps) p->partGuessed = false;
    } else if (key == "newPart") {
        const std::string id = JPPartMatcher::newPart(*ps.front(), m_job.library().store(), m_job.job().parts);
        for (JPPlacement* p : chosen()) {
            p->partId = id;
            p->partGuessed = false;
        }
    }
    edited();
}

void JPlacerParts::onEntryField(size_t page, const std::string& key, const std::string& text) {
    const JPEntry e = entryOn(page);
    std::string error;
    if (!JPEntryFields::set(m_job.job().parts, e, key, text, error)) {
        say(error.empty() ? "That cannot be changed here." : error);
        show();
        return;
    }
    edited();
}

void JPlacerParts::onEntryChoice(size_t page, const std::string& key, int index) {
    const JPEntry e = entryOn(page);
    JPPartsStore& s = m_job.job().parts;
    const JPPartsStore& lib = m_job.library().store();
    const std::vector<std::string>& ids = key == "package" ? m_packageChoices : m_footprintChoices;
    if (index < 0 || size_t(index) >= ids.size() || e.id.empty()) return;
    std::string id = ids[size_t(index)];
    if (id.rfind(kLibraryPrefix, 0) == 0)
        id = JPLibrarySync::bringIntoJob(s, { key == "package" ? K::Package : K::Footprint, id.substr(4) }, lib);
    if (key == "package" && e.kind == K::Part) {
        JPPart* p = s.part(e.id);
        p->packageId = id;
        ++p->revision;
    } else if (key == "footprint" && e.kind == K::Package) {
        JPPackage* k = s.package(e.id);
        k->footprintId = id;
        ++k->revision;
    }
    edited();
}

void JPlacerParts::copyToLibrary(const JPEntry& e) {
    JPPartsStore& s = m_job.job().parts;
    JPPartsStore& lib = m_job.library().store();
    const std::vector<std::string> diffs = JPLibrarySync::wouldChange(s, e, lib);
    auto copy = [this, e] {
        JPLibrarySync::copyToLibrary(m_job.job().parts, e, m_job.library().store());
        m_job.libraryChanged();
        edited();
    };
    if (diffs.empty()) {
        copy();
        return;
    }
    std::string body = "The library already has this " + std::string(JPEntry::name(e.kind))
                     + ". Copying replaces the library's version, for every job that takes it from now on:\n";
    for (const std::string& d : diffs) body += "\n  \xE2\x80\xA2 " + d;
    JDialogOptions opts;
    opts.okLabel = "Replace";
    std::weak_ptr<bool> alive = m_alive;
    JDialog::confirm("Replace the Library's Version?", body,
                     [alive, copy] {
                         if (const auto a = alive.lock(); a && *a) copy();
                     },
                     {}, opts);
}

void JPlacerParts::onEntryAction(size_t page, const std::string& key) {
    const JPEntry e = entryOn(page);
    if (e.id.empty()) return;
    JPPartsStore& s = m_job.job().parts;
    const JPPartsStore& lib = m_job.library().store();
    std::weak_ptr<bool> alive = m_alive;
    if (key == "copy") {
        copyToLibrary(e);
    } else if (key == "update") {
        JPLibrarySync::update(s, e, lib);
        edited();
    } else if (key == "keep") {
        JPLibrarySync::keep(s, e, lib);
        edited();
    } else if (key == "remove") {
        const std::string name = JPEntryFields::get(s, e, e.kind == K::Part ? "mpn" : "name");
        JDialogOptions opts;
        opts.okLabel = "Remove";
        JDialog::confirm(std::string("Remove the ") + JPEntry::name(e.kind) + "?",
                         "Remove " + (name.empty() ? std::string("this ") + JPEntry::name(e.kind) : name)
                             + " from the job? What uses it is left without one until another is chosen.",
                         [this, alive, e] {
                             if (const auto a = alive.lock(); !a || !*a) return;
                             m_job.job().parts.remove(e);
                             edited();
                         },
                         {}, opts);
    } else if (key == "importKicad") {
        JDialog::openFile("Import KiCad Footprint", { "kicad_mod" }, [this, alive, e](std::string path) {
            if (const auto a = alive.lock(); !a || !*a) return;
            JPFootprint f;
            std::string error;
            if (!JPKicadFootprint::read(path, f, error)) {
                say(error);
                return;
            }
            JPPartsStore& store = m_job.job().parts;
            const std::string id = store.add(std::move(f));
            if (JPPackage* k = store.package(e.id)) {
                k->footprintId = id;
                ++k->revision;
            }
            edited();
        });
    } else if (key == "makeDual" || key == "makeQuad") {
        const bool quad = key == "makeQuad";
        JDialog::input(quad ? "Make Quad Footprint" : "Make Dual Footprint", JPlacerEntryForm::prompt(quad),
            [this, alive, e, quad](std::string text) {
                if (const auto a = alive.lock(); !a || !*a) return;
                JPPartsStore& store = m_job.job().parts;
                JPPackage* k = store.package(e.id);
                if (!k) return;
                JPFootprint f;
                std::string error;
                if (!JPlacerEntryForm::make(quad, text, k->name, f, error)) {
                    say(error);
                    return;
                }
                const std::string id = store.add(std::move(f));
                k = store.package(e.id);
                k->footprintId = id;
                ++k->revision;
                edited();
            },
            {}, quad ? "16, 0.5, 4.95, 0.85, 0.25, 3.45" : "8, 1.27, 4.95, 1.95, 0.6");
    }
}

} // inline namespace jf
