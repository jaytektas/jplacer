// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPDriverConfig.h"
#include "JPFirmwareProfile.h"
#include "JPLink.h"
#include "JPReply.h"

#include <j/core/Signal.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

inline namespace jf {

// One G-code controller: its link, its I/O thread, and the firmware profile
// that says how to talk to it.
//
// The I/O THREAD owns the link. It sends queued commands one at a time, reads
// every line, completes each command when the profile's ok or error pattern
// arrives (or its timeout passes), and polls status reports between commands
// so position and state stay live while the caller waits.
//
// connect(), readSettings() and the get() on a returned future BLOCK: call
// them from the cell's thread, never the GUI's. Signals fire on the I/O
// thread; a widget re-posts through JMainThreadDispatcher before touching
// itself.
class JPGcodeDriver {
public:
    JPGcodeDriver(JPDriverConfig config, std::vector<JPFirmwareProfile> profiles);
    ~JPGcodeDriver();

    JPGcodeDriver(const JPGcodeDriver&)            = delete;
    JPGcodeDriver& operator=(const JPGcodeDriver&) = delete;

    // Open the link, identify the firmware (or take the configured profile),
    // and send the profile's init command. False with `error` on failure.
    bool connect(std::string& error);
    void disconnect();
    bool isConnected() const { return m_connected; }

    // Its settings, as they are now (a copy: they can change while it runs).
    JPDriverConfig config() const { return *cfg(); }
    // Which controller it is; never changes.
    const std::string& id() const { return m_id; }
    // New settings, taken while it runs: its name, timeouts, status interval
    // and commands are used from the next line on. How it connects (the
    // link, the profile, the waits while connecting) is read when it
    // connects: a change to those needs a new connection (JPCell::reconfigure).
    void setConfig(JPDriverConfig config);
    // The profile in use; null until connected.
    const JPFirmwareProfile* profile() const { return m_profile; }
    // Plugins the controller reported when it was identified.
    const std::vector<const JPFirmwareProfile::Plugin*>& plugins() const { return m_plugins; }

    // Queue a command line. `timeoutMs` 0 means the configured command
    // timeout. A command sent while disconnected fails at once.
    std::future<JPReply> send(std::string line, int timeoutMs = 0);
    // Run a named command (move, home, ...): this controller's own if it has
    // one, else its profile's. A command may be several lines (a machine's
    // whole homing sequence); they are sent in order, each waiting for its
    // answer, and the first refused stops the rest. BLOCKS, like get() on a
    // send. The reply's lines are every line's, in order. Fails at once when
    // there is no such command.
    JPReply command(const std::string& name, const std::map<std::string, std::string>& values = {},
                    int timeoutMs = 0);
    // Lines of G-code given whole (a nozzle's Z home), sent as a command's
    // are: in order, each waiting for its answer. BLOCKS.
    JPReply sendLines(const std::string& text, int timeoutMs = 0);

    // Block until the controller has finished every move it was given (the
    // profile's waitMotion command, which the controller answers only then)
    // and a status report from after that has been read: when it returns,
    // status() is where the machine stopped, not where it was a poll ago.
    JPReply waitForMotion();
    // STOP, from any thread, sent at once whatever is queued: `emergency`,
    // the firmware's reset (everything stops now; the position may be lost);
    // else a feed hold, then, once held still, the reset that throws the
    // queue away (the position kept). Commands waiting fail with "stopped".
    // False when the firmware has no way to (its profile names none).
    bool halt(bool emergency);

    // Read the controller's stored settings into settings(). Blocking.
    bool readSettings(std::string& error);
    std::map<std::string, std::string> settings() const;
    // A per-axis stored setting (stepsPerMm, maxRate, ...) as a number.
    std::optional<double> axisSetting(const std::string& key, const std::string& letter) const;

    // The latest status report, positions in WORK coordinates (the ones
    // G-code moves use): a report in machine coordinates has the controller's
    // last reported offset taken off.
    JPFirmwareProfile::Status status() const;

    JSignal<JPFirmwareProfile::Status> onStatus;
    // Every line sent (`sent` true) and received, for the console.
    JSignal<bool, std::string>         onTraffic;
    // An error or alarm the controller raised outside any command.
    JSignal<std::string>               onAlarm;
    // The link failed; the reason.
    JSignal<std::string>               onLost;

private:
    using Clock = std::chrono::steady_clock;

    struct Pending {
        std::string            line;
        int                    timeoutMs = 0;
        std::promise<JPReply>  promise;
    };

    void ioLoop();
    void handleLine(const std::string& line);
    void finish(JPReply reply);
    void failAll(const std::string& why);
    std::future<JPReply> failed(const std::string& why);
    bool identify(std::string& error);

    // The settings as they are now: swapped whole by setConfig, read whole
    // (cfg) by the I/O thread and the cell's, so neither sees half of a change.
    std::shared_ptr<const JPDriverConfig> cfg() const { return m_config.load(); }
    // What ends a line sent: the link's "lineEnding" (LF, CR or CRLF; LF when not given).
    std::string lineEnding() const;
    const std::string                                 m_id;
    std::atomic<std::shared_ptr<const JPDriverConfig>> m_config;
    std::vector<JPFirmwareProfile> m_profiles;
    std::unique_ptr<JPLink>        m_link;

    // The profile whose reply patterns the I/O thread reads with: a candidate
    // while identifying, then the chosen one.
    std::atomic<const JPFirmwareProfile*>        m_replyProfile{ nullptr };
    const JPFirmwareProfile*                     m_profile = nullptr;
    std::vector<const JPFirmwareProfile::Plugin*> m_plugins;
    std::atomic<bool>                            m_connected{ false };

    std::thread       m_io;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_statusNow{ false };   // ask for a status report without waiting for the interval
    std::atomic<int>  m_halt{ 0 };            // asked for: 1 a stop, 2 an emergency stop
    // A stop under way: held, waiting for the motion to come to rest.
    bool              m_holding = false;
    std::chrono::steady_clock::time_point m_holdUntil;
    std::map<std::string, double> m_holdLast;   // the positions in the last report while held

    // The I/O thread's own: the command on the wire, when it times out, and
    // the lines it has drawn so far.
    std::optional<Pending> m_inFlight;
    Clock::time_point      m_deadline;
    JPReply                m_collected;

    mutable std::mutex                  m_mutex;   // guards the members below
    std::deque<Pending>                 m_queue;
    std::map<std::string, std::string>  m_settings;
    std::map<std::string, double>       m_offsets;   // the work offset, by axis letter, as last reported
    JPFirmwareProfile::Status           m_status;
    uint64_t                            m_statusCount = 0;   // reports read, for waiting on the next
    std::condition_variable             m_statusRead;
};

} // inline namespace jf
