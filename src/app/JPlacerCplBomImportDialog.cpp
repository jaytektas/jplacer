// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerCplBomImportDialog.h"

#include "common/JPlacerLog.h"
#include "import/JPKicadBoardFile.h"
#include "ui/JPGroupFrame.h"
#include "ui/JPUiParts.h"

#include <j/core/Dialog.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/Log.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>

inline namespace jf {

namespace {

using I = JPImportField::Id;

constexpr int    kFileRows = 4;       // files the list shows before it scrolls
constexpr int    kSamples = 3;        // a column's first values shown beside it
constexpr size_t kListed = 12;        // designators listed before "and N more"
constexpr int    kExamples = 3;       // a disagreement's designators shown

const std::vector<std::pair<const char*, JPLengthUnit>> kUnits { { "Millimetres", JPLengthUnit::Millimeters },
                                                                 { "Mils", JPLengthUnit::Mils },
                                                                 { "Inches", JPLengthUnit::Inches } };

std::string fileName(const std::string& path) { return std::filesystem::path(path).filename().string(); }

// Up to kListed of `names`, then how many more.
std::string listed(const std::vector<std::string>& names) {
    std::string s;
    for (size_t i = 0; i < names.size() && i < kListed; ++i) s += (i ? ", " : "") + names[i];
    if (names.size() > kListed) s += " and " + std::to_string(names.size() - kListed) + " more";
    return s;
}

std::string now() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tmv{};
    localtime_r(&t, &tmv);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tmv);
    return buf;
}

} // namespace

JPlacerCplBomImportDialog::JPlacerCplBomImportDialog(JPConfiguration& config, std::function<void(JPBoard&)> onImported,
                                                     JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Import Board from CPL and BOM", kW, kH, hal, sx, sy, parent)
    , m_config(config)
    , m_onImported(std::move(onImported)) {
    setResizable(true, kW * 2 / 3, kH * 2 / 3);
    std::string error;
    if (!m_profiles.load(profilesFile(), error))
        JLOGC(JPlacerLog::kBoardImport, JLogLevel::Warn) << error << ": no import profiles read";

    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);

    // Files.
    auto files = std::make_unique<JPGroupFrame>(g, "Files");
    files->setAlignItems(JAlignItems::Stretch);
    m_files = files->add(std::make_unique<JDataGrid>(g, std::vector<std::string>{ "File", "What it is", "Columns read by", "Rows" }));
    const float gridH = st.gridHeaderHeight + st.gridRowHeight * kFileRows + st.scrollBarWidth;
    m_files->setVSizePolicy(JSizePolicyMode::Fixed);
    m_files->setSize(0.f, gridH);
    m_files->onSelectionChanged.connect([this](int i) {
        if (i >= 0 && i != m_chosen) {
            m_chosen = i;
            m_refillColumns = true;
        }
    });
    auto fileButtons = JPUiParts::row(g);
    m_cplButton = fileButtons->add(JPUiParts::button(g, "Choose Placement File…"));
    m_cplButton->setTooltip("The file of each part's designator, position, rotation and side (a CPL, KiCad's .pos)");
    m_cplButton->onClicked.connect([this] { addFile(JPImportSource::Role::Cpl); });
    m_addButton = fileButtons->add(JPUiParts::button(g, "Add BOM or Table…"));
    m_addButton->setTooltip("A BOM, or any table naming designators (a supplier's order, a second BOM): joined to the "
                            "placements by designator");
    m_addButton->onClicked.connect([this] { addFile(JPImportSource::Role::Bom); });
    m_removeButton = fileButtons->add(JPUiParts::button(g, "Remove"));
    m_removeButton->setTooltip("Take the file chosen out of the import");
    m_removeButton->onClicked.connect([this] { removeSource(); });
    files->add(std::move(fileButtons));
    files->setVSizePolicy(JSizePolicyMode::Fixed);
    files->setSize(0.f, gridH + st.spacing + st.buttonHeight + JPGroupFrame::extraHeight());
    m_content->add(std::move(files));

    // The columns of the file chosen, beside what the import makes.
    auto middle = JPUiParts::row(g);
    middle->setAlignItems(JAlignItems::Stretch);
    middle->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto columns = std::make_unique<JPGroupFrame>(g, "Columns");
    m_columnsFrame = columns.get();
    columns->setAlignItems(JAlignItems::Stretch);
    columns->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    auto settings = JPUiParts::row(g);
    m_role = settings->add(std::make_unique<JComboBox>(g, std::vector<std::string>{ JPImportSource::roleLabel(JPImportSource::Role::Bom),
                                                                                  JPImportSource::roleLabel(JPImportSource::Role::Other) }));
    m_role->setTooltip("What this file is: a BOM, or another table naming designators");
    m_role->onIndexChanged.connect([this](int i) {
        if (m_chosen <= 0 || size_t(m_chosen) >= m_import.sources.size() || i < 0) return;
        m_import.sources[size_t(m_chosen)].role = i == 0 ? JPImportSource::Role::Bom : JPImportSource::Role::Other;
        m_refillFiles = m_refillSummary = m_refillColumns = true;
    });
    std::vector<std::string> unitNames;
    for (const auto& [name, unit] : kUnits) unitNames.push_back(name);
    m_units = settings->add(std::make_unique<JComboBox>(g, unitNames));
    m_units->setTooltip("The units of the file's lengths where a cell does not say (\"12.5mm\" says)");
    m_units->onIndexChanged.connect([this](int i) {
        if (size_t(m_chosen) >= m_import.sources.size() || i < 0 || size_t(i) >= kUnits.size()) return;
        m_import.sources[size_t(m_chosen)].units = kUnits[size_t(i)].second;
        m_refillSummary = true;
    });
    m_profile = settings->add(std::make_unique<JComboBox>(g));
    m_profile->setTooltip("How the columns are read: guessed from their names, or a profile saved before");
    m_profile->onIndexChanged.connect([this](int i) {
        if (size_t(m_chosen) >= m_import.sources.size() || i < 0) return;
        JPImportSource& s = m_import.sources[size_t(m_chosen)];
        const std::string name = i == 0 ? std::string() : m_profile->currentText();
        if (name == s.profile) return;
        if (name.empty()) {
            s.guess();
            s.profile.clear();
        } else {
            for (const auto& p : m_profiles.profiles)
                if (p.name == name && p.role == s.role) JPMappingProfiles::apply(p, s);
        }
        m_refillColumns = m_refillSummary = m_refillFiles = true;
    });
    JButton* save = settings->add(JPUiParts::button(g, "Save Profile…"));
    save->setTooltip("Keep these columns' meanings under a name: the next file with the same columns is read by it");
    save->onClicked.connect([this] { saveProfile(); });
    columns->add(std::move(settings));
    m_columns = columns->add(std::make_unique<JScrollArea>(g, 0.f, 0.f));
    m_columns->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    middle->add(std::move(columns));

    auto makes = std::make_unique<JPGroupFrame>(g, "What it makes");
    makes->setAlignItems(JAlignItems::Stretch);
    makes->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    m_summary = makes->add(std::make_unique<JScrollArea>(g, 0.f, 0.f));
    m_summary->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_create = makes->add(std::make_unique<JCheckBox>(g, "Create Missing Parts", 0.f));
    m_create->setTooltip("A part the library does not have becomes the board's own (kept in the board, not the library); "
                         "else it is left for you to choose");
    m_create->onStateChanged.connect([this](bool on) {
        m_import.createMissing = on;
        m_refillSummary = true;
    });
    middle->add(std::move(makes));
    m_content->add(std::move(middle));
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject)->setTooltip("Close without importing");
    m_importButton = m_buttons->addButton("Import", JDialogButtonBox::Role::Accept);
    m_importButton->setTooltip("Make the placements and parts into the board chosen");
    m_buttons->onReject.connect([this] { close(); });
    m_buttons->onAccept.connect([this] { runImport(); });
    add(m_buttons.get());

    m_refillFiles = m_refillColumns = m_refillSummary = true;
}

std::string JPlacerCplBomImportDialog::profilesFile() const {
    return (std::filesystem::path(m_config.directory()) / JPMappingProfiles::kFile).string();
}

void JPlacerCplBomImportDialog::addFile(JPImportSource::Role role) {
    JDialogRequest req;
    req.kind = JDialogRequest::JKind::OpenFile;
    req.title = role == JPImportSource::Role::Cpl ? "Choose Placement File" : "Add BOM or Table";
    req.filters = role == JPImportSource::Role::Cpl
                    ? std::vector<JFileFilter>{ { "Placement files", { "csv", "pos", "txt", "tsv", "xy", "kicad_pcb" } } }
                    : std::vector<JFileFilter>{ { "Tables", { "csv", "txt", "tsv" } } };
    if (!m_import.sources.empty()) req.startPath = std::filesystem::path(m_import.sources.front().table.path).parent_path().string();
    std::weak_ptr<bool> alive = m_alive;
    req.onInput = [this, alive, role](std::string path) {
        if (const auto a = alive.lock(); !a || !*a) return;
        addSource(path, role);
    };
    JDialog::chooseFile(std::move(req));
}

void JPlacerCplBomImportDialog::addSource(const std::string& path, JPImportSource::Role role) {
    JPImportSource s;
    s.role = role;
    std::string error;
    // A KiCad board is read for its placements and footprints; anything else as a table.
    const bool board = role == JPImportSource::Role::Cpl && JPKicadBoardFile::is(path);
    if (board ? !JPKicadBoardFile::read(path, s, error) : !JPTableFile::read(path, s.table, error)) {
        JDialog::message("Cannot Read the File", fileName(path) + ": " + error);
        return;
    }
    if (const JPMappingProfiles::Profile* p = m_profiles.best(s.table.header, role)) JPMappingProfiles::apply(*p, s);
    else s.guess();
    JLOGC(JPlacerLog::kBoardImport, JLogLevel::Info) << JPImportSource::roleLabel(role) << " " << path << ": "
                                                     << s.table.header.size() << " column(s), " << s.table.rows.size()
                                                     << " row(s), read by " << (s.profile.empty() ? "guess" : s.profile)
                                                     << (board ? ", " + std::to_string(s.footprints.size()) + " footprint(s)" : std::string());
    const bool haveCpl = !m_import.sources.empty() && m_import.sources.front().role == JPImportSource::Role::Cpl;
    if (role == JPImportSource::Role::Cpl) {
        if (haveCpl) m_import.sources.front() = s;
        else m_import.sources.insert(m_import.sources.begin(), s);
        m_chosen = 0;
    } else {
        m_import.sources.push_back(s);
        m_chosen = int(m_import.sources.size()) - 1;
    }
    m_import.winners.clear();
    m_refillFiles = m_refillColumns = m_refillSummary = true;
}

void JPlacerCplBomImportDialog::removeSource() {
    if (m_chosen < 0 || size_t(m_chosen) >= m_import.sources.size()) return;
    m_import.sources.erase(m_import.sources.begin() + m_chosen);
    m_import.winners.clear();
    m_chosen = std::min(m_chosen, int(m_import.sources.size()) - 1);
    m_refillFiles = m_refillColumns = m_refillSummary = true;
}

void JPlacerCplBomImportDialog::fillFiles() {
    std::vector<std::vector<std::string>> rows;
    for (const JPImportSource& s : m_import.sources)
        rows.push_back({ fileName(s.table.path), JPImportSource::roleLabel(s.role),
                         s.profile.empty() ? "their names (guessed)" : s.profile, std::to_string(s.table.rows.size()) });
    m_files->setRows(rows);
    m_files->setSelectedIndex(m_chosen);
    const bool haveCpl = !m_import.sources.empty() && m_import.sources.front().role == JPImportSource::Role::Cpl;
    m_cplButton->setLabel(haveCpl ? "Change Placement File…" : "Choose Placement File…");
    m_removeButton->setEnabled(!m_import.sources.empty());
}

void JPlacerCplBomImportDialog::fillColumns() {
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_columns->clearChildren();
    const bool any = m_chosen >= 0 && size_t(m_chosen) < m_import.sources.size();
    m_role->setVisible(any && m_chosen > 0);   // the placement file is the placement file
    m_units->setEnabled(any);
    m_profile->setEnabled(any);
    if (!any) {
        m_columnsFrame->setTitle("Columns");
        return;
    }
    JPImportSource& s = m_import.sources[size_t(m_chosen)];
    m_columnsFrame->setTitle("Columns of " + fileName(s.table.path));
    if (m_chosen > 0) m_role->setCurrentIndex(s.role == JPImportSource::Role::Other ? 1 : 0);
    for (size_t i = 0; i < kUnits.size(); ++i)
        if (kUnits[i].second == s.units) m_units->setCurrentIndex(int(i));
    std::vector<std::string> profiles { "Their names (guessed)" };
    int current = 0;
    for (const auto& p : m_profiles.profiles)
        if (p.role == s.role) {
            if (p.name == s.profile) current = int(profiles.size());
            profiles.push_back(p.name);
        }
    m_profile->setItems(profiles);
    m_profile->setCurrentIndex(current);

    // One row a column: its header, what it holds, its first values.
    std::vector<std::string> fields;
    for (const I f : JPImportField::all()) fields.push_back(JPImportField::label(f));
    float headerW = 0;
    for (const std::string& h : s.table.header) headerW = std::max(headerW, JTextHelper::measureWidth(h));
    headerW = std::min(headerW + 2 * st.spacing, JTextHelper::measureWidth("A rather long column header"));
    float fieldW = 0;
    for (const std::string& f : fields) fieldW = std::max(fieldW, JTextHelper::measureWidth(f));
    fieldW += 4 * st.fieldPadding + st.controlHeight;
    for (size_t c = 0; c < s.table.header.size(); ++c) {
        auto row = JPUiParts::row(g);
        row->setVSizePolicy(JSizePolicyMode::Fixed);
        row->setSize(0.f, st.controlHeight);
        JLabel* header = row->add(std::make_unique<JLabel>(g, s.table.header[c].empty() ? "(no name)" : s.table.header[c], headerW));
        header->setHSizePolicy(JSizePolicyMode::Fixed);
        header->setTooltip(s.table.header[c]);
        JComboBox* field = row->add(std::make_unique<JComboBox>(g, fields, fieldW));
        field->setHSizePolicy(JSizePolicyMode::Fixed);
        const auto& all = JPImportField::all();
        field->setCurrentIndex(int(std::find(all.begin(), all.end(), s.mapping[c]) - all.begin()));
        field->setTooltip("What this column holds");
        field->onIndexChanged.connect([this, c](int i) {
            if (size_t(m_chosen) >= m_import.sources.size() || i < 0 || size_t(i) >= JPImportField::all().size()) return;
            JPImportSource& src = m_import.sources[size_t(m_chosen)];
            if (c >= src.mapping.size()) return;
            src.mapping[c] = JPImportField::all()[size_t(i)];
            src.profile.clear();   // a profile's mapping changed is not that profile any more
            m_refillSummary = m_refillFiles = true;
        });
        std::string samples;
        int shown = 0;
        for (const auto& r : s.table.rows) {
            if (shown == kSamples) break;
            if (c < r.size() && !r[c].empty()) {
                samples += (shown++ ? "  ·  " : "") + r[c];
            }
        }
        JLabel* sample = row->add(std::make_unique<JLabel>(g, samples, 0.f));
        sample->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        sample->setTooltip(samples);
        m_columns->addChildWidget(std::move(row));
    }
}

void JPlacerCplBomImportDialog::fillSummary() {
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_summary->clearChildren();
    auto line = [&](const std::string& text) {
        JLabel* l = m_summary->addChildWidget(std::make_unique<JLabel>(g, text, 0.f));
        l->setWordWrap(true);
        const float w = std::max(st.controlHeight, m_summary->bounds().width - st.scrollBarWidth - 2 * st.spacing);
        l->setMinimumSize(0.f, std::max(st.labelHeight, l->heightFor(w)));
        return l;
    };
    const bool haveCpl = !m_import.sources.empty() && m_import.sources.front().role == JPImportSource::Role::Cpl;
    if (!haveCpl) {
        line("Choose the placement file (CPL, KiCad's .pos, or a KiCad board's .kicad_pcb for its footprints too): each "
             "part's designator, position, rotation and side. Then add the BOM, if there is one, for each part's value, "
             "footprint, manufacturer, MPN and supplier.");
        m_importButton->setEnabled(false);
        return;
    }
    JPBoard board;
    JPCplBomImport::Report report;
    std::string error;
    const bool ok = m_import.build(m_config, now(), board, report, error);
    m_importButton->setEnabled(ok && report.placements > 0);
    if (!ok) {
        line(error);
        return;
    }
    line(std::to_string(report.placements) + " placement(s)" +
         (report.doNotPlace ? ", " + std::to_string(report.doNotPlace) + " not to be placed" : std::string()) +
         (report.fiducials ? ", " + std::to_string(report.fiducials) + " fiducial(s)" : std::string()));
    std::string parts = std::to_string(report.parts) + " part(s): " + std::to_string(report.matched) + " from the library";
    if (report.local) parts += ", " + std::to_string(report.local) + " the board's own";
    if (report.unmatched) parts += ", " + std::to_string(report.unmatched) + " to be chosen";
    line(parts);
    if (report.footprints)
        line(std::to_string(report.footprints) + " footprint(s) from the board file: the pads of the board's own parts' packages");
    if (report.noPart)
        line(std::to_string(report.noPart) + " placement(s) with no part named (no value, footprint or MPN in any "
             "file): add the BOM, or choose their parts after importing");
    if (!report.otherOnly.empty()) line("Not in the placement file (not placed): " + listed(report.otherOnly));
    if (!report.cplOnly.empty()) line("In the placement file only (no BOM line): " + listed(report.cplOnly));
    for (const std::string& p : report.problems) line(p);

    // Each field the files disagree on: where, and which file's to take.
    std::map<I, std::vector<const JPCplBomImport::Conflict*>> byField;
    for (const auto& c : report.conflicts) byField[c.field].push_back(&c);
    std::vector<std::string> names;
    for (const JPImportSource& s : m_import.sources) names.push_back(fileName(s.table.path));
    for (const auto& [field, list] : byField) {
        std::string text = std::string(JPImportField::label(field)) + ": the files disagree for " + std::to_string(list.size())
                         + " placement(s), as ";
        for (size_t i = 0; i < list.size() && int(i) < kExamples; ++i) {
            text += (i ? "; " : "") + list[i]->designator + " (";
            for (size_t k = 0; k < list[i]->values.size(); ++k)
                text += (k ? ", " : "") + names[size_t(list[i]->values[k].first)] + " \"" + list[i]->values[k].second + "\"";
            text += ")";
        }
        line(text);
        auto row = JPUiParts::row(g);
        row->setVSizePolicy(JSizePolicyMode::Fixed);
        row->setSize(0.f, st.controlHeight);
        row->add(std::make_unique<JLabel>(g, std::string("Take ") + JPImportField::label(field) + " from", 0.f));
        JComboBox* from = row->add(std::make_unique<JComboBox>(g, names));
        from->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        from->setCurrentIndex(m_import.winner(field));
        from->setTooltip("Whose " + std::string(JPImportField::label(field)) + " is taken where they disagree");
        from->onIndexChanged.connect([this, f = field](int i) {
            if (i < 0) return;
            m_import.winners[f] = i;
            m_refillSummary = true;
        });
        m_summary->addChildWidget(std::move(row));
    }
}

void JPlacerCplBomImportDialog::saveProfile() {
    if (size_t(m_chosen) >= m_import.sources.size()) return;
    const JPImportSource& s = m_import.sources[size_t(m_chosen)];
    const std::string suggested = !s.profile.empty() ? s.profile : std::filesystem::path(s.table.path).stem().string();
    std::weak_ptr<bool> alive = m_alive;
    JDialog::input("Save Profile", "A name for how this file's columns are read (the next file with the same columns is "
                                   "read the same way):",
                   [this, alive, chosen = m_chosen, suggested](std::string name) {
                       if (const auto a = alive.lock(); !a || !*a) return;
                       if (name.empty()) name = suggested;   // the name shown in the empty field
                       if (name.empty() || size_t(chosen) >= m_import.sources.size()) return;
                       JPImportSource& src = m_import.sources[size_t(chosen)];
                       m_profiles.put(JPMappingProfiles::from(src, name));
                       std::string error;
                       if (!m_profiles.save(profilesFile(), error)) {
                           JDialog::message("Profile Not Saved", error);
                           return;
                       }
                       src.profile = name;
                       m_refillColumns = m_refillFiles = true;
                   },
                   {}, suggested);
}

void JPlacerCplBomImportDialog::runImport() {
    JPBoard board;
    JPCplBomImport::Report report;
    std::string error;
    if (!m_import.build(m_config, now(), board, report, error)) {
        JDialog::message("Import Error", error);
        return;
    }
    JLOGC(JPlacerLog::kBoardImport, JLogLevel::Info) << "imported " << report.placements << " placement(s), " << report.parts
                                                     << " part(s) (" << report.matched << " from the library, " << report.local
                                                     << " the board's own, " << report.unmatched << " to be chosen) from "
                                                     << m_import.sources.size() << " file(s)";
    close();
    if (m_onImported) m_onImported(board);
}

void JPlacerCplBomImportDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
    // What an event changed, remade now that it is over.
    if (m_refillFiles) {
        m_refillFiles = false;
        fillFiles();
    }
    if (m_refillColumns) {
        m_refillColumns = false;
        fillColumns();
    }
    if (m_refillSummary) {
        m_refillSummary = false;
        fillSummary();
    }
}

} // inline namespace jf
