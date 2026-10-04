// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerJob.h"

#include "JPlacerChoiceDialog.h"
#include "JPlacerSettings.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"

#include <j/config/Settings.h>
#include <j/core/Dialog.h>
#include <j/core/Log.h>

#include <filesystem>

inline namespace jf {

namespace {
// OpenPnP's recent jobs: ten.
constexpr int kRecentMost = 10;

constexpr int kStatusMs = 8000;
// The file dialog filters by the last extension; ".job.xml" is added on save.
constexpr const char* kFilter = "xml";

std::string withExtension(std::string path) {
    std::string lower;
    for (const char c : path) lower += char(std::tolower(static_cast<unsigned char>(c)));
    if (!lower.ends_with(JPJob::kExtension)) path += JPJob::kExtension;
    return path;
}

} // namespace

JPlacerJob::JPlacerJob(JAppWindow& window) : m_window(window), m_config(JPlacerPaths::configDir()) {
    std::vector<std::string> problems;
    std::string error;
    if (!m_config.load(problems, error)) {
        JLOGC(JPlacerLog::kApp, JLogLevel::Error) << "configuration: " << error;
        m_window.showStatus("The parts and packages could not be read: " + error, kStatusMs);
    }
    for (const std::string& p : problems) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << p;
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << m_config.parts().size() << " part(s), " << m_config.packages().size()
                                             << " package(s), " << m_config.boards().size() << " board(s), "
                                             << m_config.panels().size() << " panel(s)";
    if (const std::string last = JSettings::instance().get<std::string>(JPlacerSettings::kJobFile, ""); !last.empty())
        if (!openPath(last, error)) JLOGC(JPlacerLog::kApp, JLogLevel::Warn) << "the last job: " << error;
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
    const std::map<int, Watcher> now = m_watchers;
    for (const auto& [id, w] : now)
        if (m_watchers.count(id)) w(what);
}

void JPlacerJob::changed() {
    m_job->dirty = true;
    title();
    notify(Change::Job);
}

void JPlacerJob::configurationChanged() {
    std::string error;
    if (!m_config.save(error)) m_window.showStatus("The configuration was not saved: " + error, kStatusMs);
    notify(Change::Configuration);
}

void JPlacerJob::title() {
    const std::string name = m_job->file.empty() ? kUntitled : std::filesystem::path(m_job->file).filename().string();
    m_window.setTitle(std::string("jplacer - ") + (m_job->dirty ? "*" : "") + name);
}

void JPlacerJob::settle(std::function<void()> then) {
    if (!m_job->dirty) {
        then();
        return;
    }
    // Yes saves first, No lets the changes go, Cancel (or closing) stays.
    const std::string name = m_job->file.empty() ? kUntitled : std::filesystem::path(m_job->file).filename().string();
    std::weak_ptr<bool> alive = m_alive;
    m_window.openModal<JPlacerChoiceDialog>(
        "Save job? - " + name, "Do you want to save your changes?\nIf you don't save, your changes will be lost.",
        std::vector<std::string> { "Yes", "No", "Cancel" }, 2, [this, alive, then](int choice) {
            if (const auto a = alive.lock(); !a || !*a) return;
            if (choice == 0) {
                if (m_job->file.empty()) saveAsThen(then);
                else if (writeTo(m_job->file)) then();
            } else if (choice == 1) {
                then();
            }
        });
}

std::vector<std::string> JPlacerJob::recentJobs() const {
    std::vector<std::string> out;
    for (int i = 0; i < kRecentMost; ++i) {
        const std::string path = JSettings::instance().get<std::string>(JPlacerSettings::kJobRecent + std::to_string(i), "");
        std::error_code ec;
        if (!path.empty() && std::filesystem::exists(path, ec)) out.push_back(path);
    }
    return out;
}

void JPlacerJob::addRecent(const std::string& path) {
    std::vector<std::string> recent = recentJobs();
    std::erase(recent, path);
    recent.insert(recent.begin(), path);
    if (recent.size() > size_t(kRecentMost)) recent.resize(size_t(kRecentMost));
    for (int i = 0; i < kRecentMost; ++i)
        JSettings::instance().set(JPlacerSettings::kJobRecent + std::to_string(i),
                                  size_t(i) < recent.size() ? recent[size_t(i)] : std::string());
    JPlacerSettings::save();
    if (onRecentChanged) onRecentChanged();
}

void JPlacerJob::openRecent(const std::string& path) {
    settle([this, path] {
        std::string error;
        if (!openPath(path, error)) {
            JDialog::message("Job Load Error", error);
            return;
        }
        title();
        notify(Change::Job);
    });
}

bool JPlacerJob::mayClose() {
    if (!m_job->dirty || m_closing) return true;
    settle([this] {
        m_closing = true;
        m_window.requestClose();
    });
    return false;
}

void JPlacerJob::newJob() {
    settle([this] {
        m_job = std::make_unique<JPJob>();
        JSettings::instance().set(JPlacerSettings::kJobFile, std::string());
        JPlacerSettings::save();
        title();
        notify(Change::Job);
    });
}

bool JPlacerJob::openPath(const std::string& path, std::string& error) {
    auto job = m_config.loadJob(path, error);
    if (!job) return false;
    m_job = std::move(job);
    JSettings::instance().set(JPlacerSettings::kJobFile, m_job->file);
    JPlacerSettings::save();
    addRecent(m_job->file);
    JLOGC(JPlacerLog::kApp, JLogLevel::Info) << "job " << m_job->file << ": " << m_job->boardLocations().size()
                                             << " board(s)";
    return true;
}

void JPlacerJob::open() {
    settle([this] {
        std::weak_ptr<bool> alive = m_alive;
        JDialog::openFile("Open Job", { kFilter }, [this, alive](std::string path) {
            if (const auto a = alive.lock(); !a || !*a) return;
            std::string error;
            if (!openPath(path, error)) {
                JDialog::message("Job Load Error", error);
                return;
            }
            title();
            notify(Change::Job);
        });
    });
}

bool JPlacerJob::writeTo(const std::string& path) {
    std::string error;
    if (!m_config.saveJob(*m_job, path, error)) {
        JDialog::message("Job Save Error", error);
        return false;
    }
    JSettings::instance().set(JPlacerSettings::kJobFile, m_job->file);
    JPlacerSettings::save();
    addRecent(m_job->file);
    title();
    return true;
}

void JPlacerJob::save() {
    if (m_job->file.empty()) saveAs();
    else writeTo(m_job->file);
}

void JPlacerJob::saveAsThen(std::function<void()> then) {
    std::weak_ptr<bool> alive = m_alive;
    JDialog::saveFile("Save Job As...", { kFilter }, [this, alive, then](std::string chosen) {
        if (const auto a = alive.lock(); !a || !*a) return;
        const std::string path = withExtension(chosen);
        auto write = [this, path, then] {
            if (writeTo(path) && then) then();
        };
        // A file already there is replaced only when asked.
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            write();
            return;
        }
        JDialogOptions opts;
        opts.okLabel = "Yes";
        opts.cancelLabel = "No";
        JDialog::confirm("Replace file?",
                         std::filesystem::path(path).filename().string() + " already exists. Do you want to replace it?",
                         write, {}, opts);
    });
}

void JPlacerJob::saveAs() {
    saveAsThen(nullptr);
}

} // inline namespace jf
