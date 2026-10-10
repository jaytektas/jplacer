// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPartsPanel.h"

#include "common/JPWhen.h"
#include "common/JPlacerLog.h"

#include "JPVisionPipelineActions.h"

#include "JPFieldGrid.h"
#include "setup/JPFormBuilder.h"
#include "setup/JPVisionForms.h"
#include "JPUiParts.h"

#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"

#include <j/core/Dialog.h>
#include <j/core/FrameTimer.h>
#include <j/core/JLabel.h>
#include <j/core/Log.h>
#include <j/core/JSeparator.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

inline namespace jf {

namespace {

// OpenPnP's search box is fifteen characters wide.
constexpr int kSearchColumns = 15;

std::unique_ptr<JSeparator> toolSeparator(JSceneGraph& graph) {
    return std::make_unique<JSeparator>(graph, JSeparator::JOrientation::Vertical, JPIconButton::size());
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

JPPartsPanel::JPPartsPanel(JSceneGraph& graph, JPConfiguration& config, double split)
    : JContainer(graph), m_config(config), m_model(config) {
    JPUiParts::asPanel(*this);
    const JStyle& st = JStyle::current();
    m_model.onChanged = [this](JPBoard* board) { changed(board); };

    // The toolbar, and the search box at its right.
    auto bar = JPUiParts::row(graph);
    auto tool = [&](const char* name, const char* icon, const char* tip) {
        return bar->add(std::make_unique<JPIconButton>(graph, name, icon, tip));
    };
    JPIconButton* newOne = tool("New Part...", "general-add", "Create a new part, specifying it's ID.");
    newOne->setLeads(JPIconButton::Leads::Elsewhere);
    newOne->onClicked.connect([this] { newPart(); });
    m_delete = tool("Delete Part", "general-remove", "Delete the currently selected part.");
    m_delete->onClicked.connect([this] { deleteParts(); });
    bar->add(toolSeparator(graph));
    m_pick = tool("Pick Part", "pick", "Perform a feed and pick of the selected part from the first available feeder.");
    m_pick->onClicked.connect([this] {
        if (const JPPart* p = selectedPart(); p && onPickPart) onPickPart(*p);
    });
    bar->add(toolSeparator(graph));
    m_copy = tool("Copy Part to Clipboard", "copy", "Copy the currently selected part to the clipboard in text format.");
    m_copy->onClicked.connect([this] { copyPart(); });
    tool("Create Part from Clipboard", "paste", "Create a new part from a definition on the clipboard.")
        ->onClicked.connect([this] { pastePart(); });
    bar->add(std::make_unique<JContainer>(graph, 0.f, 0.f))->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    // Show: every row, the library's, the boards', or one board's.
    JLabel* showLabel = bar->add(std::make_unique<JLabel>(graph, "Show"));
    showLabel->setFixedSize(JTextHelper::measureWidth("Show") + st.spacing, st.controlHeight);
    m_show = bar->add(std::make_unique<JComboBox>(graph, m_model.showChoices()));
    m_show->setCurrentIndex(0);   // All
    m_show->setTooltip("Which to list: all, the library's, the open boards', or one board's");
    m_show->onIndexChanged.connect([this](int) {
        m_model.setShow(m_show->currentText());
        m_table->refresh();
    });
    JLabel* searchLabel = bar->add(std::make_unique<JLabel>(graph, "Search"));
    searchLabel->setFixedSize(JTextHelper::measureWidth("Search") + st.spacing, st.controlHeight);
    m_search = bar->add(std::make_unique<JLineEdit>(graph, ""));
    m_search->setClearButtonEnabled(true);   // the ✕ takes the filter off
    m_search->setFixedSize(JTextHelper::measureWidth("M") * kSearchColumns, st.controlHeight);
    m_search->onTextChanged.connect([this](const std::string& t) { m_table->setFilter(t); });
    add(std::move(bar));

    // The table over the chosen part's tabs, a divider between to drag.
    m_tablePane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    m_tabsPane = std::make_unique<JContainer>(graph, 0.f, 0.f);
    for (JContainer* p : { m_tablePane.get(), m_tabsPane.get() })
        p->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch);
    m_table = m_tablePane->add(std::make_unique<JPTable>(graph));
    m_table->setStateKey("parts");
    m_table->setModel(&m_model);
    m_table->openMenu = [this](JMenu* m, float x, float y) { if (openMenu) openMenu(m, x, y); };
    m_table->onSelectionChanged.connect([this] {
        updateWizards();
        if (const JPCatalog::Part* e = selectedEntry(); e && onPartChosen) onPartChosen(*e);
    });
    m_table->onEditRefused = [](const std::string&) {};
    // Right-click: a board's own part copied into the library, a board's copy taken again from it.
    m_contextMenu = std::make_unique<JMenu>("Parts");
    m_addToLibrary = m_contextMenu->add(graph, "Add to Library");
    m_addToLibrary->setTooltip("Copy the board's own part (and its package, where the library has none of that name) into "
                               "the library; the board keeps its own");
    m_addToLibrary->onTriggered.connect([this] { addToLibrary(); });
    m_updateFromLibrary = m_contextMenu->add(graph, "Update from Library");
    m_updateFromLibrary->setTooltip("Take the board's copy of the library's part again, as the library has it now");
    m_updateFromLibrary->onTriggered.connect([this] { updateFromLibrary(); });
    m_table->setContextMenu(m_contextMenu.get());
    m_table->onContextMenu = [this](int) { updateWizards(); };
    m_form = m_tabsPane->add(std::make_unique<JPSetupForm>(graph));
    m_form->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_form->onChanged = [this](const std::string& property) {
        if (property.find(":parameter:") != std::string::npos) {
            if (onParameterChanged) onParameterChanged();
            act(property);
            return;
        }
        m_table->refresh();
        const JPCatalog::Part* e = selectedEntry();
        changed(e ? e->board.get() : nullptr);
        // A packaging's kind changed: its row's tape fields come or go.
        if (property.rfind("library.pkg", 0) == 0 && property.size() > 5 && property.compare(property.size() - 5, 5, ".kind") == 0) {
            std::weak_ptr<bool> alive = m_alive;
            JMainThreadDispatcher::instance().post([this, alive] {
                if (const auto a = alive.lock(); !a || !*a) return;
                m_form->remake(formFor(selectedEntry()));
            });
        }
    };
    m_form->onAction = [this](const std::string& action) { act(action); };
    m_split = add(std::make_unique<JSplitter>(graph, JSplitter::JOrientation::Vertical, 0.f, 0.f));
    m_split->setHostsPanes(true);
    m_split->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_split->addPane(m_tablePane.get(), float(split));
    m_split->addPane(m_tabsPane.get(), float(1 - split));
    updateWizards();
}

double JPPartsPanel::split() const {
    const std::vector<float> f = m_split->fractions();
    return f.empty() ? 0.5 : f.front();
}

void JPPartsPanel::refresh() {
    m_model.reload();
    // Show's boards as open now; one that has closed shows all again.
    if (const std::vector<std::string> choices = m_model.showChoices(); choices != m_show->items()) {
        const std::string was = m_show->currentText();
        m_show->setItems(choices);
        const auto at = std::find(choices.begin(), choices.end(), was);
        m_show->setCurrentIndex(at == choices.end() ? 0 : int(at - choices.begin()));
        m_model.setShow(m_show->currentText());
    }
    m_table->refresh();
    updateWizards();
}

JPBoard* JPPartsPanel::selectedBoard() const {
    const JPCatalog::Part* e = selectedEntry();
    return e ? e->board.get() : nullptr;
}

const JPCatalog::Part* JPPartsPanel::selectedEntry() const {
    const std::vector<int> rows = m_table->selectedRows();
    return rows.size() == 1 ? m_model.entry(rows.front()) : nullptr;
}

void JPPartsPanel::addToLibrary() {
    const JPCatalog::Part* e = selectedEntry();
    if (!e) return;
    std::string error;
    if (!JPCatalog::addToLibrary(m_config, *e, error)) {
        JDialog::message("Not Added to the Library", error);
        return;
    }
    const std::string name = e->name;
    JLOGC(JPlacerLog::kUi, JLogLevel::Info) << "Parts: " << name << " of " << JPCatalog::source(e->board) << " added to the library";
    refresh();
    changed(nullptr);
}

void JPPartsPanel::updateFromLibrary() {
    const JPCatalog::Part* e = selectedEntry();
    if (!e || !e->board) return;
    JPBoard* board = e->board.get();
    JPCatalog::updateFromLibrary(m_config, *e);
    refresh();
    changed(board);
}

std::vector<JPPart*> JPPartsPanel::selections() const {
    std::vector<JPPart*> out;
    for (const int r : m_table->selectedRows())
        if (JPPart* p = m_model.part(r)) out.push_back(p);
    return out;
}

const JPPart* JPPartsPanel::selectedPart() const {
    const auto s = selections();
    return s.size() == 1 ? s.front() : nullptr;
}

void JPPartsPanel::selectPart(const JPPart* part) {
    if (selectedPart() != part) m_table->selectRow(m_model.rowOf(part));
}

void JPPartsPanel::selectPart(const JPBoard* board, const std::string& boardPartKey, const JPPart* libraryPart) {
    const int row = m_model.rowOf(board, boardPartKey, libraryPart);
    if (m_table->selectedRow() != row) m_table->selectRow(row);
}

void JPPartsPanel::changed(JPBoard* board) {
    if (board) {
        if (onBoardChanged) onBoardChanged(*board);
    } else if (onChanged) {
        onChanged();
    }
}

void JPPartsPanel::updateWizards() {
    const auto chosen = selections();
    // As OpenPnP's action groups: one part for Delete, Pick and Copy; several for Delete. Only the library's are
    // deleted here (a board's are its own: the Boards tab's).
    bool library = !m_table->selectedRows().empty();
    for (const int r : m_table->selectedRows())
        if (const JPCatalog::Part* e = m_model.entry(r); !e || e->board) library = false;
    m_delete->setEnabled(library);
    m_pick->setEnabled(chosen.size() == 1);
    m_copy->setEnabled(chosen.size() == 1);
    const JPCatalog::Part* e = selectedEntry();
    m_addToLibrary->setEnabled(e && e->status == JPCatalog::Status::Own);
    m_updateFromLibrary->setEnabled(e && e->status == JPCatalog::Status::LibraryChanged);

    const int row = m_table->selectedRows().size() == 1 ? m_table->selectedRows().front() : -1;
    const std::string id = e ? m_model.rowKey(row) : std::string();
    const std::string vision = visionShown(e ? e->part() : nullptr);
    if (id == m_shownPart && vision == m_shownVision) {
        m_form->refresh();
        return;
    }
    m_shownPart = id;
    m_shownVision = vision;
    m_form->setForm(formFor(e));
}

std::string JPPartsPanel::visionShown(const JPPart* p) const {
    if (!p) return "";
    const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
    const JPVisionSettings* b = m_config.inheritedVision(*p, JPVisionSettings::Kind::Bottom, defaults.first);
    const JPVisionSettings* f = m_config.inheritedVision(*p, JPVisionSettings::Kind::Fiducial, defaults.second);
    return (b ? b->id : std::string()) + "|" + (f ? f->id : std::string());
}

JPSetupProperties::Form JPPartsPanel::formFor(const JPCatalog::Part* e) {
    JPSetupProperties::Form form;
    const JPPart* p = e ? e->part() : nullptr;
    if (e && !JPCatalog::editable(e->status)) {
        // A board's copy of the library's, or a part to be chosen: what it is, not edited here.
        JPFormBuilder add(form);
        add.tab("Board");
        add.group(JPCatalog::source(e->board) + ": " + e->name);
        add.note(std::string(JPCatalog::statusName(e->status)) + ". " + JPCatalog::statusTip(e->status) + ".");
        if (e->status == JPCatalog::Status::LibraryChanged)
            add.button("updateFromLibrary", "Update from Library", "Take the board's copy again, as the library has it now.");
        return form;
    }
    if (p) {
        JPFormBuilder add(form);
        // The library's pages for the library's (a board's own lives in the board: no library page, no stock).
        if (!e->board) {
            libraryPage(add, p->id);
            stockPage(add, p->id);
        }
        // Settings: the pick conditions.
        add.tab("Settings");
        add.group("Pick Conditions");
        const std::string partId = p->id;
        add.integer("pickRetryCount", "Feed & Pick Retry Count",
                    [this, partId] {
                        const JPPart* part = m_config.part(partId);
                        return part ? part->pickRetryCount : 0;
                    },
                    [this, partId](int v) {
                        if (JPPart* part = m_config.part(partId)) part->pickRetryCount = v;
                    },
                    0, 1000);
        add.tip("The number of retries for the feed and pick process for each placement. The nozzle is cleared (and "
                "part is discarded) after a failed attempt. Each retry is consecutive.");
        // The vision settings it uses (its own, its package's, the machine's).
        const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
        const JPVisionForms::Holder holder { JPVisionForms::Holder::Kind::Part, partId };
        for (const auto kind : { JPVisionSettings::Kind::Bottom, JPVisionSettings::Kind::Fiducial }) {
            const bool bottom = kind == JPVisionSettings::Kind::Bottom;
            if (const JPVisionSettings* v = m_config.inheritedVision(*p, kind, bottom ? defaults.first : defaults.second)) {
                std::string used;
                for (const std::string& u :
                     m_config.visionUsedIn(*v, bottom ? defaults.first : defaults.second, bottom ? "Bottom Vision" : "Fiducal Locator"))
                    used += (used.empty() ? "" : ", ") + u;
                JPVisionForms::addPage(add, m_config, v->id, used, holder, tests());
            }
        }
    }
    return form;
}

void JPPartsPanel::libraryPage(JPFormBuilder& add, const std::string& partId) {
    auto part = [this, partId]() -> JPPart* { return m_config.libraryPart(partId); };
    const JPPart* p = part();
    if (!p) return;
    add.tab("Library");
    add.group("Part");
    add.row("Value");
    add.text("library.value", "Value", [part] { return part() ? part()->value : std::string(); },
             [part](const std::string& v) {
                 if (JPPart* q = part()) q->value = v;
             });
    add.tip("Its electrical value as written (100n, 4k7): matched to a board's however written (100nF, 0.1uF)");
    add.row("Datasheet");
    add.text("library.datasheet", "Datasheet", [part] { return part() ? part()->datasheet : std::string(); },
             [part](const std::string& v) {
                 if (JPPart* q = part()) q->datasheet = v;
             });
    add.tip("A link to its datasheet, or its file");
    add.note("Known to the library for good as " + p->uuid + ", whatever it is named.");

    // Identifiers: exact names (an MPN and its manufacturer, a supplier's part number and the supplier).
    add.group("Identifiers");
    add.note("A board part with one of these (its MPN, or a supplier's part number) is this part, without asking.");
    const std::vector<std::string> kinds { "MPN", "Supplier PN" };
    add.header({ "Kind", "Manufacturer / Supplier", "Code" });
    for (size_t i = 0; i < p->identifiers.size(); ++i) {
        const std::string key = "library.id" + std::to_string(i) + ".";
        auto id = [part, i]() -> JPPart::Identifier* {
            JPPart* q = part();
            return q && i < q->identifiers.size() ? &q->identifiers[i] : nullptr;
        };
        add.row(std::to_string(i + 1));
        add.choice(key + "kind", "Kind", kinds, [id] { return std::string(id() && id()->kind == "supplierPn" ? "Supplier PN" : "MPN"); },
                   [id](const std::string& v) {
                       if (auto* x = id()) x->kind = v == "Supplier PN" ? "supplierPn" : "mpn";
                   });
        add.text(key + "org", "Manufacturer / Supplier", [id] { return id() ? id()->org : std::string(); },
                 [id](const std::string& v) {
                     if (auto* x = id()) x->org = v;
                 });
        add.text(key + "code", "Code", [id] { return id() ? id()->code : std::string(); },
                 [id](const std::string& v) {
                     if (auto* x = id()) x->code = v;
                 });
        add.iconButton("library:removeId:" + std::to_string(i), "general-remove", "Delete this identifier");
        add.leading();
        add.end();
    }
    add.endColumns();
    add.row("");
    add.iconButton("library:addId", "general-add", "Add an identifier: an MPN, or a supplier's part number");
    add.leading();
    add.words("Add an identifier");
    add.end();
    add.wideButton("library:manufacturers", "Manufacturers' Names…",
                   "The manufacturers the library knows and the other names files give them (TI is Texas Instruments)");

    // Also known as: what boards' files have called it (learned when a part is chosen for one, or typed).
    add.group("Also Known As");
    add.note("What boards' files call it: a board part called so is matched to it. Learned when this part is chosen "
             "for one with Remember ticked; typed here too.");
    const std::vector<std::string> fields { "Value and footprint", "Value", "Footprint" };
    auto fieldLabel = [](const std::string& f) {
        return std::string(f == "valueFootprint" ? "Value and footprint" : f == "footprint" ? "Footprint" : "Value");
    };
    add.header({ "Calls it by", "As (value|footprint)", "Learned from" });
    for (size_t i = 0; i < p->akas.size(); ++i) {
        const std::string key = "library.aka" + std::to_string(i) + ".";
        auto aka = [part, i]() -> JPPart::Aka* {
            JPPart* q = part();
            return q && i < q->akas.size() ? &q->akas[i] : nullptr;
        };
        add.row(std::to_string(i + 1));
        add.choice(key + "field", "Calls it by", fields, [aka, fieldLabel] { return aka() ? fieldLabel(aka()->field) : std::string(); },
                   [aka](const std::string& v) {
                       if (auto* x = aka()) x->field = v == "Value and footprint" ? "valueFootprint" : v == "Footprint" ? "footprint" : "value";
                   });
        add.text(key + "text", "As", [aka] { return aka() ? aka()->text : std::string(); },
                 [aka](const std::string& v) {
                     if (auto* x = aka()) x->text = v;
                 });
        const JPPart::Aka& a = p->akas[i];
        add.words(a.learnedFrom.empty() ? std::string("typed") : a.learnedFrom + (a.when.empty() ? "" : ", " + JPWhen::withAgo(a.when)));
        add.iconButton("library:removeAka:" + std::to_string(i), "general-remove", "Delete this name");
        add.leading();
        add.end();
    }
    add.endColumns();
    add.row("");
    add.iconButton("library:addAka", "general-add", "Add a name boards' files call it by");
    add.leading();
    add.words("Add a name");
    add.end();

    // How it comes: tape (cut or reel), tray, tube, loose; the part's rotation as it sits, set once here.
    add.group("Packagings");
    add.note("How the part comes. A tape's width, pocket pitch and kind (paper or embossed), and the part's rotation "
             "as it sits in the packaging (pin 1 against the tape's sprocket holes at 0), set once here.");
    const std::vector<std::string> packagingKinds(std::begin(JPPart::kPackagingKinds), std::end(JPPart::kPackagingKinds));
    add.header({ "Kind", "Tape [mm]", "Pitch [mm]", "Tape", "Rotation [°]", "Quantity", "Note" });
    for (size_t i = 0; i < p->packagings.size(); ++i) {
        const std::string key = "library.pkg" + std::to_string(i) + ".";
        auto k = [part, i]() -> JPPart::Packaging* {
            JPPart* q = part();
            return q && i < q->packagings.size() ? &q->packagings[i] : nullptr;
        };
        const bool tape = p->packagings[i].kind == "Cut tape" || p->packagings[i].kind == "Reel";
        add.row(std::to_string(i + 1));
        add.choice(key + "kind", "Kind", packagingKinds, [k] { return k() ? k()->kind : std::string(); },
                   [k](const std::string& v) {
                       if (auto* x = k()) x->kind = v;
                   });
        if (tape) {
            add.choice(key + "width", "Tape [mm]", { "8", "12", "16", "24", "32", "44", "56" },
                       [k] { return k() ? std::to_string(int(k()->tapeWidthMm)) : std::string(); },
                       [k](const std::string& v) {
                           if (auto* x = k()) x->tapeWidthMm = std::strtod(v.c_str(), nullptr);
                       });
            add.number(key + "pitch", "Pitch [mm]", [k] { return k() ? k()->pitchMm : 0.0; },
                       [k](double v) {
                           if (auto* x = k(); x && v > 0) x->pitchMm = v;
                       }, 1);
            add.choice(key + "tape", "Tape", { "Paper", "Embossed" }, [k] { return k() ? k()->tapeType : std::string(); },
                       [k](const std::string& v) {
                           if (auto* x = k()) x->tapeType = v;
                       });
        } else {
            for (int s = 0; s < 3; ++s) add.skip();
        }
        add.number(key + "rotation", "Rotation [°]", [k] { return k() ? k()->rotationDeg : 0.0; },
                   [k](double v) {
                       if (auto* x = k()) x->rotationDeg = v;
                   }, 1);
        add.integer(key + "quantity", "Quantity", [k] { return k() ? k()->quantity : 0; },
                    [k](int v) {
                        if (auto* x = k()) x->quantity = v;
                    }, 0, 10000000);
        add.text(key + "note", "Note", [k] { return k() ? k()->note : std::string(); },
                 [k](const std::string& v) {
                     if (auto* x = k()) x->note = v;
                 });
        add.iconButton("library:removePackaging:" + std::to_string(i), "general-remove", "Delete this packaging");
        add.leading();
        add.end();
    }
    add.endColumns();
    add.row("");
    add.iconButton("library:addPackaging", "general-add", "Add a way the part comes");
    add.leading();
    add.words("Add a packaging");
    add.end();

    // Where it is bought.
    add.group("Offers");
    add.note("Where the part is bought: the supplier's part number (SKU), the packaging, the least they sell, their "
             "price breaks (\"1: 0.0100, 100: 0.0050\"), a link, and the last price seen.");
    std::vector<std::string> offerPackagings { "" };
    for (const char* kind : JPPart::kPackagingKinds) offerPackagings.push_back(kind);
    add.header({ "Supplier", "SKU", "Packaging", "MOQ", "Price breaks", "Last price", "Link" });
    for (size_t i = 0; i < p->offers.size(); ++i) {
        const std::string key = "library.offer" + std::to_string(i) + ".";
        auto o = [part, i]() -> JPPart::Offer* {
            JPPart* q = part();
            return q && i < q->offers.size() ? &q->offers[i] : nullptr;
        };
        auto field = [&add, o, &key](const char* name, const char* label, std::string JPPart::Offer::*member) {
            add.text(key + name, label, [o, member] { return o() ? (*o()).*member : std::string(); },
                     [o, member](const std::string& v) {
                         if (auto* x = o()) (*x).*member = v;
                     });
        };
        add.row(std::to_string(i + 1));
        field("supplier", "Supplier", &JPPart::Offer::supplier);
        field("sku", "SKU", &JPPart::Offer::sku);
        add.choice(key + "packaging", "Packaging", offerPackagings, [o] { return o() ? o()->packaging : std::string(); },
                   [o](const std::string& v) {
                       if (auto* x = o()) x->packaging = v;
                   });
        add.integer(key + "moq", "MOQ", [o] { return o() ? o()->moq : 0; },
                    [o](int v) {
                        if (auto* x = o()) x->moq = v;
                    }, 0, 10000000);
        field("breaks", "Price breaks", &JPPart::Offer::priceBreaks);
        field("price", "Last price", &JPPart::Offer::lastPrice);
        field("link", "Link", &JPPart::Offer::link);
        add.iconButton("library:removeOffer:" + std::to_string(i), "general-remove", "Delete this offer");
        add.leading();
        add.end();
    }
    add.endColumns();
    add.row("");
    add.iconButton("library:addOffer", "general-add", "Add where the part is bought");
    add.leading();
    add.words("Add an offer");
    add.end();
}

bool JPPartsPanel::libraryAct(const std::string& action) {
    if (action.rfind("library:", 0) != 0) return false;
    JPPart* p = selectedPart() ? m_config.libraryPart(selectedPart()->id) : nullptr;
    if (!p) return true;
    const std::string what = action.substr(8);
    auto index = [&what](const std::string& prefix) -> long {
        return what.rfind(prefix, 0) == 0 ? std::strtol(what.c_str() + prefix.size(), nullptr, 10) : -1;
    };
    if (what == "manufacturers") {
        if (openManufacturers) openManufacturers();
        return true;
    }
    if (what == "addId") p->identifiers.push_back({ "mpn", "", "" });
    else if (what == "addPackaging") p->packagings.push_back({});
    else if (what == "addOffer") p->offers.push_back({});
    else if (const long i = index("removePackaging:"); i >= 0 && size_t(i) < p->packagings.size()) p->packagings.erase(p->packagings.begin() + i);
    else if (const long i = index("removeOffer:"); i >= 0 && size_t(i) < p->offers.size()) p->offers.erase(p->offers.begin() + i);
    else if (what == "addAka") p->akas.push_back({ "valueFootprint", "", "", "" });
    else if (const long i = index("removeId:"); i >= 0 && size_t(i) < p->identifiers.size()) p->identifiers.erase(p->identifiers.begin() + i);
    else if (const long k = index("removeAka:"); k >= 0 && size_t(k) < p->akas.size()) p->akas.erase(p->akas.begin() + k);
    changed();
    remakeLater();
    return true;
}

void JPPartsPanel::remakeLater() {
    // The page made again after this click (its button is on it).
    std::weak_ptr<bool> alive = m_alive;
    JMainThreadDispatcher::instance().post([this, alive] {
        if (const auto a = alive.lock(); !a || !*a) return;
        m_form->remake(formFor(selectedEntry()));   // the tab and where it was scrolled to kept
    });
}

void JPPartsPanel::stockPage(JPFormBuilder& add, const std::string& partId) {
    const JPPart* p = m_config.libraryPart(partId);
    if (!p) return;
    JPStockStore& stock = m_config.stock();
    const std::string uuid = p->uuid;
    m_lots = stock.lots(uuid, false);
    const size_t closed = stock.lots(uuid, true).size() - m_lots.size();
    add.tab("Stock");
    add.group("Stock");
    long long onHand = 0;
    for (const JPStockLot& l : m_lots) onHand += l.onHand;
    std::string about = m_lots.empty() ? std::string("None in stock.")
                                       : std::to_string(onHand) + " in stock, in " + std::to_string(m_lots.size()) + " lot(s).";
    if (closed > 0) about += " " + std::to_string(closed) + " closed lot(s) kept with their ledgers.";
    add.note(about + " Stock is what you have, not what is loaded; a lot's figure is its ledger's.");
    if (!m_lots.empty()) add.header({ "Lot", "Packaging", "Holds", "On feeder", "Where kept", "Date code", "Note" });
    for (size_t i = 0; i < m_lots.size(); ++i) {
        const std::string key = "stock.lot" + std::to_string(i) + ".";
        // A lot's own field: shown from what was read, kept at once when changed.
        auto field = [this, &add, &key, i](const char* name, const char* label, std::string JPStockLot::*member) {
            add.text(key + name, label, [this, i, member] { return i < m_lots.size() ? m_lots[i].*member : std::string(); },
                     [this, i, member](const std::string& v) {
                         if (i >= m_lots.size()) return;
                         JPStockLot l = m_lots[i];
                         l.*member = v;
                         std::string error;
                         if (m_config.stock().updateLot(l, error)) m_lots[i] = l;
                         else JDialog::message("Not Changed", error);
                     });
        };
        add.row(std::to_string(i + 1));
        field("label", "Lot", &JPStockLot::label);
        add.words(m_lots[i].packaging);
        add.words(std::to_string(m_lots[i].onHand));
        const JPFeeder* feeder = m_lots[i].feederId.empty() ? nullptr : m_config.feeder(m_lots[i].feederId);
        add.words(feeder ? feeder->name() : m_lots[i].feederId.empty() ? std::string("-") : std::string("(a feeder now gone)"));
        field("location", "Where kept", &JPStockLot::location);
        field("dateCode", "Date code", &JPStockLot::dateCode);
        field("note", "Note", &JPStockLot::note);
        add.button("stock:ledger:" + std::to_string(i), "Ledger…",
                   "Every change to this lot, and what it held after each; add what was used, lost or counted");
        add.end();
    }
    add.endColumns();
    add.wideButton("stock:receive", "Receive Stock…", "A new lot of this part: an order that came in, or one found and counted");

    add.group("Attrition");
    add.note("How many to allow for parts lost to mis-picks and drops, as a share of those placed: a job's shortages "
             "add it to what it needs.");
    add.row("Attrition [%]");
    add.text("stock.attrition", "Attrition [%]",
             [this, uuid] {
                 const auto set = m_config.stock().attritionSet(uuid);
                 char buf[32] = "";
                 if (set) std::snprintf(buf, sizeof buf, "%g", *set * 100);
                 return std::string(buf);
             },
             [this, uuid](const std::string& v) {
                 std::optional<double> rate;
                 if (!v.empty()) {
                     char* end = nullptr;
                     const double percent = std::strtod(v.c_str(), &end);
                     if (!end || *end != '\0' || percent < 0 || percent > 100) return;
                     rate = percent / 100;
                 }
                 std::string error;
                 if (!m_config.stock().setAttrition(uuid, rate, error)) JDialog::message("Not Changed", error);
             });
    add.tip("Empty: what the ledger measured is used");
    add.end();
    const JPStockStore::Attrition a = stock.attrition(uuid);
    char measured[160];
    if (a.used > 0)
        std::snprintf(measured, sizeof measured, "Measured: %lld lost of %lld taken (%.1f %%).", a.lost, a.used + a.lost, a.rate() * 100);
    else if (a.lost > 0)
        std::snprintf(measured, sizeof measured, "Not measured yet: none used, so the %lld lost are not a share of anything.", a.lost);
    else
        std::snprintf(measured, sizeof measured, "Not measured yet: the ledger has nothing used or lost.");
    add.note(measured);
}

bool JPPartsPanel::stockAct(const std::string& action) {
    if (action.rfind("stock:", 0) != 0) return false;
    const JPPart* p = selectedPart() ? m_config.libraryPart(selectedPart()->id) : nullptr;
    if (!p) return true;
    auto changed = [this] { remakeLater(); };
    if (action == "stock:receive") {
        if (openReceive) openReceive(*p, changed);
    } else if (action.rfind("stock:ledger:", 0) == 0) {
        const size_t i = std::strtoul(action.c_str() + 13, nullptr, 10);
        if (i < m_lots.size() && openLedger) openLedger(m_lots[i].uuid, p->id, changed);
    }
    return true;
}

bool JPPartsPanel::pipelineAct(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& what) {
    JPVisionPipelineActions::Hooks hooks;
    hooks.edit = [this, holder](const std::string& id) {
        if (editPipeline) editPipeline(id, holder);
    };
    hooks.preview = [this, holder](const std::string& id, const std::string& parameter) {
        if (previewParameter) previewParameter(id, holder, parameter);
    };
    hooks.machineDefault = [this](JPVisionSettings::Kind kind) {
        const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
        return m_config.visionSettings(kind == JPVisionSettings::Kind::Bottom ? defaults.first : defaults.second);
    };
    hooks.test = [this, holder](const std::string& id, const std::string& test) {
        if (visionTest) visionTest(id, holder, test);
    };
    hooks.changed = [this] {
        // Its sliders follow the pipeline.
        m_shownPart.clear();
        updateWizards();
        m_table->refresh();
        changed(selectedBoard());
    };
    return JPVisionPipelineActions::act(m_config, settingsId, what, hooks);
}

void JPPartsPanel::act(const std::string& action) {
    if (action == "updateFromLibrary") {
        updateFromLibrary();
        return;
    }
    if (libraryAct(action) || stockAct(action)) return;
    const JPPart* p = selectedPart();
    if (!p) return;
    const size_t colon = action.find(':');
    if (colon == std::string::npos) return;
    const bool bottom = action.substr(0, colon) == "bottom";
    const std::string what = action.substr(colon + 1);
    const auto defaults = machineDefaults ? machineDefaults() : std::pair<std::string, std::string> {};
    const JPVisionSettings* v = m_config.inheritedVision(*p, bottom ? JPVisionSettings::Kind::Bottom : JPVisionSettings::Kind::Fiducial,
                                                         bottom ? defaults.first : defaults.second);
    if (!v) return;
    const std::string id = v->id;
    const JPVisionForms::Holder holder { JPVisionForms::Holder::Kind::Part, p->id };
    if (pipelineAct(id, holder, what)) return;
    auto run = [this, id, what, holder, machineDefault = bottom ? defaults.first : defaults.second] {
        std::string why;
        if (JPVisionForms::act(m_config, id, what, holder, machineDefault, why)) {
            m_shownPart.clear();
            updateWizards();
            m_table->refresh();
            changed(selectedBoard());
        } else if (!why.empty()) {
            JDialog::message("Error", why);
        }
    };
    if (what == "reset") {
        JDialogOptions opts;
        opts.okLabel = "Yes";
        opts.cancelLabel = "No";
        JDialog::confirm("Reset to Default", std::string("This will reset the ") + (bottom ? "bottom" : "fiducial") +
                                                 " vision settings with to the default settings. Are you sure??",
                         run, nullptr, opts);
        return;
    }
    if (what == "generalize") {
        // What it goes back to, and that its own stay, said first.
        const JPVisionForms::Manage m = JPVisionForms::manageFor(m_config, *v, holder);
        JDialogOptions opts;
        opts.okLabel = "Yes";
        opts.cancelLabel = "No";
        JDialog::confirm(m.generalizeLabel, m.generalizeTip + ".\n\nAre you sure?", run, nullptr, opts);
        return;
    }
    // Not while the button clicked is still in its page: the page is made again.
    jPostToNextFrame(run);
}

void JPPartsPanel::newPart() {
    if (m_config.packages().empty()) {
        JDialog::message("Error", "There are currently no packages defined in the system. Please create at least one "
                                  "package before creating a part.");
        return;
    }
    JDialog::input("New Part", "Please enter an ID for the new part.", [this](std::string text) {
        const std::string id = trimmed(text);
        if (id.empty()) return;
        if (m_config.part(id)) {
            JDialog::message("Error", "Part ID " + id + " already exists.");
            newPart();   // and asked again, as OpenPnP asks
            return;
        }
        auto part = std::make_shared<JPPart>();
        part->id = id;
        part->packageId = m_config.packages().front()->id;
        m_config.addPart(part);
        refresh();
        selectPart(part.get());
        changed();
    });
}

void JPPartsPanel::deleteParts() {
    const auto chosen = selections();
    if (chosen.empty()) return;
    std::string ids;
    for (size_t i = 0; i < chosen.size() && i < 3; ++i) ids += (i ? ", " : "") + chosen[i]->id;
    if (chosen.size() > 3) ids += ", and " + std::to_string(chosen.size() - 3) + " others";
    std::vector<std::string> doomed;
    for (const JPPart* p : chosen) doomed.push_back(p->id);
    JDialogOptions opts;
    opts.okLabel = "Yes";
    opts.cancelLabel = "No";
    JDialog::confirm("Delete " + std::to_string(chosen.size()) + " parts?", "Are you sure you want to delete " + ids + "?",
        [this, doomed] {
            for (const std::string& id : doomed) m_config.removePart(id);   // the library's only (updateWizards)
            refresh();
            changed();
        },
        nullptr, opts);
}

void JPPartsPanel::copyPart() {
    const JPPart* p = selectedPart();
    if (!p) return;
    std::string text = JPXmlWriter::text(p->toXml());
    if (!text.empty() && text.back() == '\n') text.pop_back();
    JWidget::clipboardSet(text);
}

void JPPartsPanel::pastePart() {
    JDialog::input("Create Part from Clipboard", "Please enter an ID for the pasted part.", [this](std::string text) {
        const std::string id = trimmed(text);
        if (id.empty()) return;
        if (m_config.part(id)) {
            JDialog::message("Error", "Part ID " + id + " already exists.");
            pastePart();
            return;
        }
        JPXmlElement root;
        std::string error;
        if (!JPXmlReader::parse(JWidget::clipboardGet(), root, error) || root.name != "part") {
            JDialog::message("Paste Failed", error.empty() ? std::string("The clipboard does not hold a part.") : error);
            return;
        }
        auto part = std::make_shared<JPPart>(JPPart::fromXml(root));
        part->id = id;
        m_config.addPart(part);
        refresh();
        selectPart(part.get());
        changed();
    });
}

} // inline namespace jf
