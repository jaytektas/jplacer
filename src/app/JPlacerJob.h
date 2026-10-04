// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "model/JPConfiguration.h"
#include "model/JPJob.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

inline namespace jf {

// The open job and the configuration it draws on (OpenPnP's Configuration:
// parts, packages, boards and panels, kept in jplacer's configuration
// folder): File > New Job, Open Job…, Save Job and Save Job As…, and the
// window's title as OpenPnP's ("jplacer - *Untitled.job.xml"). The job open
// last is opened again at start (JPlacerSettings::kJobFile). Views watch it
// and are told what changed.
class JPlacerJob {
public:
    static constexpr const char* kUntitled = "Untitled.job.xml";

    enum class Change {
        Job,             // another job, or the job's boards and panels
        Configuration,   // parts, packages, boards or panels
    };
    using Watcher = std::function<void(Change)>;

    explicit JPlacerJob(JAppWindow& window);
    ~JPlacerJob();

    JPJob&                 job() { return *m_job; }
    const JPJob&           job() const { return *m_job; }
    JPConfiguration&       configuration() { return m_config; }
    const JPConfiguration& configuration() const { return m_config; }
    JAppWindow&            window() { return m_window; }

    void newJob();
    void open();
    // File > Open Recent Job: the jobs opened or saved last (ten at most,
    // newest first, those still there), and opening one.
    std::vector<std::string> recentJobs() const;
    void openRecent(const std::string& path);
    // The recent jobs changed (the menu made again).
    std::function<void()> onRecentChanged;
    void save();
    void saveAs();
    // After an edit to the job: marked changed, and the views told.
    void changed();
    // A job is running: the job is not left (new, open, recent) until it stops.
    std::function<bool()> running;
    // After an edit to parts, packages, boards or panels: the configuration
    // saved (said in the status bar when it cannot be), and the views told.
    void configurationChanged();

    // For the window's close: false (and a question asked) while the job
    // has changes not saved; the window is closed again once answered.
    bool mayClose();

    int  watch(Watcher w);
    void unwatch(int id);

private:
    void settle(std::function<void()> then);
    bool openPath(const std::string& path, std::string& error);
    bool writeTo(const std::string& path);
    void saveAsThen(std::function<void()> then);
    void title();
    void notify(Change what);
    void addRecent(const std::string& path);

    JAppWindow&             m_window;
    JPConfiguration         m_config;
    std::unique_ptr<JPJob>  m_job = std::make_unique<JPJob>();
    bool                    m_closing = false;
    std::map<int, Watcher>  m_watchers;
    int                     m_nextWatcher = 1;
    std::shared_ptr<bool>   m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
