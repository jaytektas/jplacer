// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerJob.h"

#include "JPlacerSettings.h"

#include "common/JPlacerLog.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/Log.h>

#include <filesystem>

inline namespace jf {

namespace {

constexpr int         kStatusMs = 8000;
constexpr const char* kAppName  = "jplacer";

std::string withExtension(std::string path) {
    if (std::filesystem::path(path).extension() != std::string(".") + JPJob::kExtension)
        path += std::string(".") + JPJob::kExtension;
    return path;
}

} // namespace

JPlacerJob::JPlacerJob(JAppWindow& window) : m_window(window) {
    JSettings& s = JSettings::instance();
    std::string folder = s.get<std::string>(JPlacerSettings::kLibraryFolder, "");
    if (folder.empty()) folder = JPLibrary::defaultFolder();
    if (!m_library.open(folder)) m_window.showStatus(m_library.problem(), kStatusMs);

    if (const std::string last = s.get<std::string>(JPlacerSettings::kJobFile, ""); !last.empty()) {
        std::string error;
        if (!openPath(last, error)) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "the last job: " << error;
    }
    title();
}

JPlacerJob::~JPlacerJob() {
    *m_alive = false;
}

int JPlacerJob::watch(Watcher w) {
    m_watchers[m_nextWatcher] = std::move(w);
    return m_nextWatcher++;
}

void JPlacerJob::unwatch(int id) {
    m_watchers.erase(id);
}

void JPlacerJob::notify(Change what) {
    // A watcher may unwatch (or another watch) while told.
    const std::map<int, Watcher> now = m_watchers;
    for (const auto& [id, w] : now)
        if (m_watchers.count(id)) w(what);
}

void JPlacerJob::changed(Change what) {
    m_modified = true;
    title();
    notify(what);
}

bool JPlacerJob::libraryChanged() {
    std::string error;
    const bool ok = m_library.save(error);
    if (!ok) m_window.showStatus("The library was not saved: " + error, kStatusMs);
    notify(Change::Library);
    return ok;
}

void JPlacerJob::setLibraryFolder(const std::string& folder) {
    JSettings::instance().set(JPlacerSettings::kLibraryFolder, folder);
    JPlacerSettings::save();
    if (!m_library.open(folder.empty() ? JPLibrary::defaultFolder() : folder)) m_window.showStatus(m_library.problem(), kStatusMs);
    else m_window.showStatus("Parts library: " + m_library.folder(), kStatusMs);
    notify(Change::Library);
}

void JPlacerJob::title() {
    std::string name;
    if (!m_path.empty()) name = std::filesystem::path(m_path).stem().string();
    else if (!m_job.board.name.empty()) name = m_job.board.name;
    if (name.empty() && !m_modified) {
        m_window.setTitle(kAppName);
        return;
    }
    m_window.setTitle((name.empty() ? std::string("Untitled job") : name) + (m_modified ? "*" : "") + " \xE2\x80\x94 " + kAppName);
}

void JPlacerJob::settle(std::function<void()> then) {
    if (!m_modified) {
        then();
        return;
    }
    JDialogOptions opts;
    opts.okLabel = "Save";
    opts.cancelLabel = "Don't Save";
    // Only a button answers: closing the question must not throw the changes away.
    opts.closeOnEscape = false;
    opts.showCloseButton = false;
    const std::string name = m_path.empty() ? std::string("This job") : std::filesystem::path(m_path).filename().string();
    std::weak_ptr<bool> alive = m_alive;
    JDialog::confirm("Unsaved Changes", name + " has changes that are not saved. Save them?",
        [this, alive, then] {
            if (const auto a = alive.lock(); !a || !*a) return;
            if (m_path.empty()) saveAsThen(then);
            else if (writeTo(m_path)) then();
        },
        [alive, then] {
            if (const auto a = alive.lock(); !a || !*a) return;
            then();
        },
        opts);
}

bool JPlacerJob::mayClose() {
    if (!m_modified || m_closing) return true;
    settle([this] {
        m_closing = true;
        m_window.requestClose();
    });
    return false;
}

void JPlacerJob::newJob(std::function<void()> then) {
    settle([this, then] {
        m_job = JPJob();
        m_path.clear();
        m_modified = false;
        JSettings::instance().set(JPlacerSettings::kJobFile, std::string());
        JPlacerSettings::save();
        title();
        notify(Change::Board);
        if (then) then();
    });
}

bool JPlacerJob::openPath(const std::string& path, std::string& error) {
    JPJob job;
    if (!JPJob::load(path, job, error)) return false;
    m_job = std::move(job);
    m_path = path;
    m_modified = false;
    JSettings::instance().set(JPlacerSettings::kJobFile, path);
    JPlacerSettings::save();
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "job " << path << ": " << m_job.board.placements.size()
                                             << " placement(s), " << m_job.parts.parts.size() << " part(s)";
    return true;
}

void JPlacerJob::open() {
    settle([this] {
        std::weak_ptr<bool> alive = m_alive;
        JDialog::openFile("Open Job", { JPJob::kExtension }, [this, alive](std::string path) {
            if (const auto a = alive.lock(); !a || !*a) return;
            std::string error;
            if (!openPath(path, error)) {
                JDialog::message("The job could not be opened", error);
                return;
            }
            title();
            notify(Change::Board);
        });
    });
}

bool JPlacerJob::writeTo(const std::string& path) {
    std::string error;
    if (!m_job.save(path, error)) {
        JDialog::message("The job could not be saved", error);
        return false;
    }
    m_path = path;
    m_modified = false;
    JSettings::instance().set(JPlacerSettings::kJobFile, path);
    JPlacerSettings::save();
    title();
    m_window.showStatus("Saved " + path, kStatusMs);
    return true;
}

void JPlacerJob::save() {
    if (m_path.empty()) saveAs();
    else writeTo(m_path);
}

void JPlacerJob::saveAsThen(std::function<void()> then) {
    std::weak_ptr<bool> alive = m_alive;
    JDialog::saveFile("Save Job As", { JPJob::kExtension }, [this, alive, then](std::string path) {
        if (const auto a = alive.lock(); !a || !*a) return;
        if (writeTo(withExtension(path)) && then) then();
    });
}

void JPlacerJob::saveAs() {
    saveAsThen(nullptr);
}

} // inline namespace jf
