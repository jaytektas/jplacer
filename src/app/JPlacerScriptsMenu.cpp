// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerScriptsMenu.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>
#include <j/core/MainThreadDispatcher.h>
#include <j/platform/JDesktop.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <thread>

inline namespace jf {

namespace fs = std::filesystem;

namespace {
// How long the status line says how a script went: done briefly, failed long enough to read.
constexpr int kStatusMs = 3000;
constexpr int kErrorMs  = 8000;
}

JPlacerScriptsMenu::JPlacerScriptsMenu(JAppWindow& window, JSceneGraph& graph, std::shared_ptr<JPScripting> scripting, JMenu* menu)
    : m_window(window), m_graph(graph), m_scripting(std::move(scripting)), m_menu(menu) {
    refresh();
    // Clear Scripting Engine Pool greyed while there is nothing to clear, as the pool changes on any thread.
    m_scripting->onPoolChanged = [this, alive = std::weak_ptr<bool>(m_alive)] {
        JMainThreadDispatcher::instance().post([this, alive] {
            if (const auto a = alive.lock(); a && *a && m_clearPool) m_clearPool->setEnabled(m_scripting->canClearPool());
        });
    };
}

JPlacerScriptsMenu::~JPlacerScriptsMenu() {
    *m_alive = false;
    m_scripting->onPoolChanged = nullptr;
}

void JPlacerScriptsMenu::refresh() {
    m_scripting->refresh();
    m_menu->clear();
    m_subMenus.clear();
    fill(m_menu, m_scripting->directory());
    m_menu->addSeparator(m_graph);
    m_menu->add(m_graph, "Refresh Scripts")->onTriggered.connect([this] { refresh(); });
    m_menu->add(m_graph, "Open Scripts Directory")->onTriggered.connect([this] {
        if (!JDesktop::openUrl(m_scripting->directory())) m_window.showStatus("The scripts folder could not be opened", kErrorMs);
    });
    m_clearPool = m_menu->add(m_graph, "Clear Scripting Engine Pool");
    m_clearPool->setEnabled(m_scripting->canClearPool());
    m_clearPool->onTriggered.connect([this] {
        const int ended = m_scripting->clearPool();
        m_window.showStatus(ended ? std::to_string(ended) + " scripting engine(s) cleared from the pool"
                                  : std::string("No scripting engines in pool, nothing to do"), kStatusMs);
    });
}

void JPlacerScriptsMenu::fill(JMenu* menu, const std::string& directory) {
    // Scripts and folders together, by name whatever the case, as OpenPnP sorts them.
    std::vector<fs::path> entries;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(directory, ec)) {
        if (e.is_regular_file() && JPScripting::runnable(e.path().string())) entries.push_back(e.path());
        else if (e.is_directory() && e.path() != fs::path(m_scripting->eventsDirectory()) && !fs::exists(e.path() / ".ignore"))
            entries.push_back(e.path());
    }
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    };
    std::sort(entries.begin(), entries.end(), [&](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    for (const fs::path& e : entries) {
        if (fs::is_directory(e)) {
            m_subMenus.push_back(std::make_unique<JMenu>(e.filename().string()));
            JMenu* sub = m_subMenus.back().get();
            fill(sub, e.string());
            menu->add(m_graph, e.filename().string(), {}, sub);
        } else {
            menu->add(m_graph, e.filename().string())->onTriggered.connect([this, path = e.string()] { run(path); });
        }
    }
}

void JPlacerScriptsMenu::run(const std::string& path) {
    const std::string name = fs::path(path).filename().string();
    m_window.showStatus("Running " + name + "\xE2\x80\xA6", kStatusMs);
    std::thread([this, path, name, scripting = m_scripting, alive = std::weak_ptr<bool>(m_alive)] {
        std::string why;
        const bool ok = scripting->execute(path, JJson::object(), why);
        if (!ok) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << why;
        JMainThreadDispatcher::instance().post([this, ok, why, name, alive] {
            if (const auto a = alive.lock(); !a || !*a) return;
            if (ok) m_window.showStatus(name + " done", kStatusMs);
            else m_window.showStatus(why, kErrorMs);
        });
    }).detach();
}

} // inline namespace jf
