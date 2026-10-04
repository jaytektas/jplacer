// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "job/JPJob.h"
#include "library/JPLibrary.h"

#include <j/app/JAppWindow.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

inline namespace jf {

// The open job and the main library: File > New Job, Open Job, Save Job and
// Save Job As, and the window's title (the
// job's name, a * while it has changes not saved). The job open last is
// opened again at start (JPlacerSettings::kJobFile). Views watch it and are
// told what changed.
class JPlacerJob {
public:
    enum class Change {
        Board,   // a new board: another job, or a file read in
        Parts,   // its parts, or which part a placement has
        Library, // the main library
    };
    using Watcher = std::function<void(Change)>;

    explicit JPlacerJob(JAppWindow& window);
    ~JPlacerJob();

    const JPJob&     job() const { return m_job; }
    JPJob&           job() { return m_job; }
    const JPLibrary& library() const { return m_library; }
    JPLibrary&       library() { return m_library; }
    const std::string& path() const { return m_path; }
    JAppWindow&      window() { return m_window; }
    bool             modified() const { return m_modified; }

    // A new, empty job (once the open one's changes are saved or let go).
    void newJob();
    void open();
    void save();
    void saveAs();
    // After an edit to the job (its parts, a placement's part): marked
    // changed, and the views told.
    void changed(Change what);

    // After an edit to the library: saved (false, and said, when it cannot
    // be), and the views told.
    bool libraryChanged();
    // The library kept in another folder from now on (JPlacerSettings::kLibraryFolder;
    // empty: the default), and opened from there.
    void setLibraryFolder(const std::string& folder);

    // For the window's close: false (and a question asked) while there are
    // changes not saved; the window is closed again once answered.
    bool mayClose();

    // A view's watcher, until the id is given back.
    int  watch(Watcher w);
    void unwatch(int id);

private:
    // Runs `then` once the job's changes are saved or let go (asked first).
    void settle(std::function<void()> then);
    bool openPath(const std::string& path, std::string& error);
    bool writeTo(const std::string& path);
    void saveAsThen(std::function<void()> then);
    void title();
    void notify(Change what);

    JAppWindow&              m_window;
    JPJob                    m_job;
    JPLibrary                m_library;
    std::string              m_path;      // empty: never saved
    bool                     m_modified = false;
    bool                     m_closing = false;   // the question was answered: close
    std::map<int, Watcher>   m_watchers;
    int                      m_nextWatcher = 1;
    std::shared_ptr<bool>    m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
