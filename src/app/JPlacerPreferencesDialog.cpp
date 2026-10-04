// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPreferencesDialog.h"

#include "JPlacerAppearance.h"
#include "JPlacerLauncher.h"
#include "JPlacerMachine.h"
#include "JPlacerSettings.h"
#include "ui/JPGroupFrame.h"
#include "ui/JPJogPanel.h"
#include "ui/JPTextField.h"
#include "ui/JPUiParts.h"

#include "library/JPLibrary.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>
#include <j/core/MainThreadDispatcher.h>
#include <j/core/MenuSystem.h>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

void store(const char* key, bool on) {
    JSettings::instance().set(key, on);
    JPlacerSettings::save();
}

// A section's title, over its rows.
std::unique_ptr<JLabel> heading(JSceneGraph& graph, const std::string& text) {
    return std::make_unique<JLabel>(graph, text, 0.f);
}


// A label as wide as `widest` of its column, then `control` taking the rest of the row.
std::unique_ptr<JContainer> labelled(JSceneGraph& graph, const std::string& text, float widest,
                                     std::unique_ptr<JWidget> control) {
    const JStyle& st = JStyle::current();
    auto row = JPUiParts::row(graph);
    auto label = std::make_unique<JLabel>(graph, text, 0.f);
    label->setFixedSize(std::ceil(widest) + 2 * st.spacing, st.labelHeight);
    row->add(std::move(label));
    control->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    row->add(std::move(control));
    return row;
}

} // namespace

JPlacerPreferencesDialog::JPlacerPreferencesDialog(std::function<void()> onCheckNow, std::function<void(double)> onScale,
                                                   JPKeyMap& keys, std::function<void()> onJogSteps,
                                                   std::function<void(std::string)> onLibraryFolder,
                                                   JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Preferences", kW, kH, hal, sx, sy, parent)
    , m_onCheckNow(std::move(onCheckNow))
    , m_keys(keys)
    , m_onJogSteps(std::move(onJogSteps)) {
    setResizable(true, kW / 2, kH / 2);
    m_tabs    = std::make_unique<JTabWidget>(graph(), 0.f, 0.f);
    m_general = generalPage(std::move(onScale), std::move(onLibraryFolder));
    m_keysTab = keysPage();
    m_jog     = jogPage();
    m_tabs->addTab("General", m_general.get());
    m_tabs->addTab("Keys", m_keysTab.get());
    m_tabs->addTab("Jog", m_jog.get());
    add(m_tabs.get());

    m_buttons = std::make_unique<JDialogButtonBox>(graph());
    m_buttons->addButton("Check Now", JDialogButtonBox::Role::Action)->onClicked.connect([this] {
        close();
        // Posted, so the check starts once this window is off the modal stack and
        // whatever it finds is shown over the main window rather than behind it.
        JMainThreadDispatcher::instance().post(m_onCheckNow);
    });
    m_buttons->addButton("Close", JDialogButtonBox::Role::Accept);
    m_buttons->onAccept.connect([this] { close(); });
    add(m_buttons.get());
}

std::unique_ptr<JLabel> JPlacerPreferencesDialog::note(const std::string& text) {
    auto l = std::make_unique<JLabel>(graph(), text, 0.f);
    l->setWordWrap(true);
    m_notes.push_back(l.get());
    return l;
}

std::unique_ptr<JContainer> JPlacerPreferencesDialog::generalPage(std::function<void(double)> onScale,
                                                                  std::function<void(std::string)> onLibraryFolder) {
    JSceneGraph& g = graph();
    auto page = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*page);
    const float widest = std::max(JTextHelper::measureWidth("Theme"), JTextHelper::measureWidth("Interface scale"));

    page->add(heading(g, "Appearance"));
    auto theme = std::make_unique<JComboBox>(g, JPlacerAppearance::themes(), 0.f);
    theme->setCurrentIndex(JPlacerSettings::theme());   // before it is watched: opening changes nothing
    theme->onIndexChanged.connect([](int i) {
        JSettings::instance().set(JPlacerSettings::kTheme, i);
        JPlacerSettings::save();
        JPlacerAppearance::applyTheme(i);
    });
    page->add(labelled(g, "Theme", widest, std::move(theme)));

    std::vector<std::string> scaleNames;
    int scaleAt = 0;
    for (const JPlacerAppearance::Scale& s : JPlacerAppearance::scales()) {
        if (s.scale == JPlacerSettings::uiScale()) scaleAt = int(scaleNames.size());
        scaleNames.push_back(s.name);
    }
    auto scale = std::make_unique<JComboBox>(g, scaleNames, 0.f);
    scale->setCurrentIndex(scaleAt);
    scale->onIndexChanged.connect([onScale = std::move(onScale)](int i) {
        const auto& scales = JPlacerAppearance::scales();
        if (i < 0 || i >= int(scales.size())) return;
        JSettings::instance().set(JPlacerSettings::kUiScale, scales[size_t(i)].scale);
        JPlacerSettings::save();
        onScale(scales[size_t(i)].scale);
    });
    page->add(labelled(g, "Interface scale", widest, std::move(scale)));

    page->add(heading(g, "General"));
    auto tearOff = std::make_unique<JCheckBox>(g, "Tear-off menus (drag a menu off into its own window)", 0.f);
    tearOff->setChecked(JPlacerSettings::tearOffMenus());
    tearOff->onStateChanged.connect([](bool on) {
        JMenuManager::instance().setTearOffEnabled(on);   // read each time a menu opens
        store(JPlacerSettings::kTearOffMenus, on);
    });
    page->add(std::move(tearOff));
    if (JPlacerLauncher::supported()) {
        auto launcher = std::make_unique<JCheckBox>(g, "Show jplacer in the applications menu", 0.f);
        launcher->setChecked(JPlacerSettings::launcher());
        launcher->onStateChanged.connect([](bool on) {
            if (on) JPlacerLauncher::install(); else JPlacerLauncher::remove();
            store(JPlacerSettings::kLauncher, on);
        });
        page->add(std::move(launcher));
    }

    page->add(heading(g, "Parts library"));
    auto folderRow = JPUiParts::row(g);
    JPTextField* folder = folderRow->add(std::make_unique<JPTextField>(g));
    folder->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    folder->setValue(JSettings::instance().get<std::string>(JPlacerSettings::kLibraryFolder, ""));
    folder->setPlaceholderText(JPLibrary::defaultFolder());
    auto choose = std::make_shared<std::function<void(std::string)>>(std::move(onLibraryFolder));
    folder->onCommitted.connect([choose](std::string text) { (*choose)(text); });
    JButton* browse = folderRow->add(JPUiParts::button(g, "Choose\xE2\x80\xA6"));
    browse->onClicked.connect([choose, folder] {
        JDialog::openFolder("Parts Library Folder", [choose, folder](std::string path) {
            folder->setValue(path);
            (*choose)(path);
        });
    });
    page->add(std::move(folderRow));
    page->add(note("Where the parts library is kept; empty for jplacer's own data folder. A git repository or a "
                   "shared drive works. Choosing another opens the library there: nothing is copied or moved, and "
                   "jobs keep their own parts."));

    page->add(heading(g, "Updates"));
    auto atStartup = std::make_unique<JCheckBox>(g, "Check for updates when jplacer opens", 0.f);
    atStartup->setChecked(JPlacerSettings::updatesAtStartup());
    atStartup->onStateChanged.connect([](bool on) { store(JPlacerSettings::kUpdatesAtStartup, on); });
    page->add(std::move(atStartup));
    auto beta = std::make_unique<JCheckBox>(g, "Include beta versions", 0.f);
    beta->setChecked(JPlacerSettings::updatesBeta());
    beta->onStateChanged.connect([](bool on) { store(JPlacerSettings::kUpdatesBeta, on); });
    page->add(std::move(beta));
    page->add(note("Beta versions get new features first and have had less testing."));
    return page;
}

std::unique_ptr<JContainer> JPlacerPreferencesDialog::keysPage() {
    JSceneGraph& g = graph();
    auto page = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*page);
    page->add(note("Click a function's key box and press the key for it (Escape leaves it as it was). A key "
                      "belongs to one function: given to another, it is taken from the first. A key typed into a "
                      "field goes to the field."));
    m_keyList = page->add(std::make_unique<JScrollArea>(g, 0.f, 0.f));
    m_keyList->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    auto bottom = JPUiParts::row(g);
    m_keyNote = bottom->add(std::make_unique<JLabel>(g, "", 0.f));
    m_keyNote->setHSizePolicy(JSizePolicyMode::Expanding, 1);
    bottom->add(JPUiParts::button(g, "Reset All"))->onClicked.connect([this] {
        m_keys.resetAll();
        showKeys();
        m_keyNote->setText("Every key is back to its default.");
    });
    page->add(std::move(bottom));
    fillKeys();
    return page;
}

void JPlacerPreferencesDialog::fillKeys() {
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_keyList->clearChildren();
    m_keyBoxes.clear();
    float widest = 0;
    for (const JPKeyMap::Function& f : m_keys.functions()) widest = std::max(widest, JTextHelper::measureWidth(f.label));
    const float boxW = std::ceil(JTextHelper::measureWidth("Ctrl+Shift+PageDown")) + 4 * st.spacing;
    // A framed group for each, as tall as its rows.
    const float rowH = std::max(st.buttonHeight, st.controlHeight);
    JPGroupFrame* frame = nullptr;
    float inner = 0;
    auto close = [&] {
        if (!frame) return;
        frame->setVSizePolicy(JSizePolicyMode::Fixed);
        frame->setSize(0.f, inner + JPGroupFrame::extraHeight());
    };
    std::string group;
    for (const JPKeyMap::Function& f : m_keys.functions()) {
        if (!frame || f.group != group) {
            close();
            group = f.group;
            frame = m_keyList->addChildWidget(std::make_unique<JPGroupFrame>(g, group));
            frame->setAlignItems(JAlignItems::Stretch);
            inner = 0;
        }
        inner += (inner > 0 ? st.spacing : 0) + rowH;
        auto row = JPUiParts::row(g);
        auto label = std::make_unique<JLabel>(g, f.label, 0.f);
        label->setFixedSize(std::ceil(widest) + 2 * st.spacing, st.labelHeight);
        row->add(std::move(label));
        JKeySequenceEdit* box = row->add(std::make_unique<JKeySequenceEdit>(g, "", boxW));
        box->setFixedSize(boxW, st.controlHeight);
        const std::string id = f.id, name = f.label;
        box->onCaptured.connect([this, id, name](JKeyEvent ke) {
            const JMenuShortcut key = JPKeyMap::fromEvent(ke);
            std::string takenFrom, why;
            if (key.key == JKeyEvent::JKey::Unknown) why = "that key cannot be told apart";
            else if (m_keys.assign(id, key, takenFrom, why)) {
                m_keyNote->setText(takenFrom.empty() ? name + ": " + JPKeyMap::format(key)
                                                     : name + ": " + JPKeyMap::format(key) + ", taken from " + takenFrom);
            }
            if (!why.empty()) m_keyNote->setText(name + ": not given (" + why + ")");
            showKeys();
        });
        row->add(JPUiParts::button(g, "Clear"))->onClicked.connect([this, id, name] {
            std::string takenFrom, why;
            m_keys.assign(id, {}, takenFrom, why);
            m_keyNote->setText(name + ": no key");
            showKeys();
        });
        m_keyBoxes[id] = box;
        frame->add(std::move(row));
    }
    close();
    showKeys();
}

void JPlacerPreferencesDialog::showKeys() {
    for (const auto& [id, box] : m_keyBoxes) box->setText(m_keys.keyText(id));
}

std::unique_ptr<JContainer> JPlacerPreferencesDialog::jogPage() {
    JSceneGraph& g = graph();
    auto page = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*page);
    const std::string distanceLabel = "Distance steps (mm or degrees)", speedLabel = "Speed steps (%)";
    const float widest = std::max(JTextHelper::measureWidth(distanceLabel), JTextHelper::measureWidth(speedLabel));

    page->add(heading(g, "Jog panel"));
    auto distances = std::make_unique<JPTextField>(g);
    distances->setValue(JPJogPanel::formatSteps(JPlacerMachine::jogDistances()));
    JPTextField* d = distances.get();
    page->add(labelled(g, distanceLabel, widest, std::move(distances)));
    auto speeds = std::make_unique<JPTextField>(g);
    std::vector<double> percent = JPlacerMachine::jogSpeeds();
    for (double& v : percent) v *= 100;
    speeds->setValue(JPJogPanel::formatSteps(percent));
    JPTextField* s = speeds.get();
    page->add(labelled(g, speedLabel, widest, std::move(speeds)));
    page->add(note("Numbers apart, smallest first. The distance slider steps through the distances; the speeds "
                      "mark the speed slider, and Faster and Slower go to the next. Each step can be given a key on "
                      "the Keys tab (1 for 1 mm, say). Empty: the steps jplacer starts with."));
    m_jogNote = page->add(note(""));

    // Taken as a whole when committed; steps that make no sense are said so,
    // and the field shows the steps in use again.
    auto take = [this](JPTextField* field, const char* key, double least, double most, bool isPercent) {
        field->onCommitted.connect([this, field, key, least, most, isPercent](std::string text) {
            std::string why;
            const bool empty = text.find_first_not_of(" \t") == std::string::npos;
            const std::vector<double> steps = empty ? std::vector<double>{} : JPJogPanel::parseSteps(text, least, most, why);
            if (!empty && steps.empty()) {
                m_jogNote->setText("Not taken: " + why + ".");
                std::vector<double> now = isPercent ? JPlacerMachine::jogSpeeds() : JPlacerMachine::jogDistances();
                if (isPercent) for (double& v : now) v *= 100;
                field->setValue(JPJogPanel::formatSteps(now));
                return;
            }
            if (empty) JSettings::instance().remove(key);
            else JSettings::instance().set(key, JPJogPanel::formatSteps(steps));
            JPlacerSettings::save();
            m_jogNote->setText("");
            std::vector<double> now = isPercent ? JPlacerMachine::jogSpeeds() : JPlacerMachine::jogDistances();
            if (isPercent) for (double& v : now) v *= 100;
            field->setValue(JPJogPanel::formatSteps(now));
            if (m_onJogSteps) m_onJogSteps();
            fillKeys();   // the steps' own keys
        });
    };
    take(d, JPlacerSettings::kJogDistances, JPlacerMachine::kLeastJogDistance, JPlacerMachine::kMostJogDistance, false);
    take(s, JPlacerSettings::kJogSpeeds, 1, 100, true);
    return page;
}

float JPlacerPreferencesDialog::pad() { return 2 * JStyle::current().fieldPadding; }

void JPlacerPreferencesDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float x = pad(), cw = w - 2 * pad();
    const float buttonsY = h - pad() - st.buttonHeight;
    const float top = contentTop();
    // A note is as tall as its lines at the page's width (the panel's padding off).
    const float noteW = cw - 2 * st.fieldPadding;
    for (JLabel* n : m_notes) n->setMinimumSize(0.f, std::max(st.labelHeight, n->heightFor(noteW)));
    // The tabs fill what the buttons leave; their pages lay themselves out.
    m_tabs->setBounds({ x, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_tabs->getNodeId(), DirtySelf);
    const JRect b = m_tabs->bounds();
    graph().computeLayout(m_tabs->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ x, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
