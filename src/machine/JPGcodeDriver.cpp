// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPGcodeDriver.h"

#include "JPLinkFactory.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <cstdlib>

inline namespace jf {

namespace {

// How long one read waits before the loop looks at its queue and the status
// timer again: the I/O thread's reaction time, not a protocol timeout.
constexpr int kReadSliceMs = 2;

} // namespace

JPGcodeDriver::JPGcodeDriver(JPDriverConfig config, std::vector<JPFirmwareProfile> profiles)
    : m_config(std::move(config)), m_profiles(std::move(profiles)) {}

JPGcodeDriver::~JPGcodeDriver() {
    disconnect();
}

bool JPGcodeDriver::connect(std::string& error) {
    if (m_connected) return true;
    m_link = JPLinkFactory::create(m_config.link, error);
    if (!m_link) return false;
    if (!m_link->open(error)) {
        m_link.reset();
        return false;
    }
    JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << m_config.name << ": opened " << m_link->describe();
    m_running = true;
    m_io = std::thread(&JPGcodeDriver::ioLoop, this);

    if (!identify(error)) {
        disconnect();
        return false;
    }
    m_connected = true;
    if (const auto init = m_profile->command("init")) {
        const JPReply r = send(*init).get();
        if (!r.ok) {
            error = m_config.name + ": the start-up command was refused (" + r.error + ")";
            disconnect();
            return false;
        }
    }
    JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << m_config.name << ": " << m_profile->name()
                                                << ", " << m_plugins.size() << " plugin(s)";
    return true;
}

bool JPGcodeDriver::identify(std::string& error) {
    if (m_config.profile != "auto") {
        for (const JPFirmwareProfile& p : m_profiles) {
            if (p.id() != m_config.profile) continue;
            m_replyProfile = &p;
            if (!p.identifyCommand().empty()) {
                const JPReply r = send(p.identifyCommand(), m_config.identifyTimeoutMs).get();
                if (r.ok) m_plugins = p.pluginsIn(r.lines);
            }
            m_profile = &p;
            return true;
        }
        error = m_config.name + ": no firmware profile '" + m_config.profile + "'";
        return false;
    }

    // Ask each way of identifying once, and offer the answer to every profile
    // that asks the same way, highest priority first. A refusal is asked
    // again once: bytes left on the line from before the port was opened
    // (a half command, a reset banner) spoil the first command, not the second.
    std::map<std::string, JPReply> asked;
    bool answered = false;   // anything at all came back
    for (const JPFirmwareProfile& p : m_profiles) {
        if (p.identifyCommand().empty()) continue;
        auto it = asked.find(p.identifyCommand());
        if (it == asked.end()) {
            m_replyProfile = &p;
            JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << m_config.name << ": identifying with '"
                                                         << p.identifyCommand() << "' (" << p.name() << " asks so)";
            JPReply r = send(p.identifyCommand(), m_config.identifyTimeoutMs).get();
            if (!r.ok && !r.error.empty() && p.errorIn(r.error)) {
                JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << m_config.name << ": refused (" << r.error << "), asking again";
                r = send(p.identifyCommand(), m_config.identifyTimeoutMs).get();
            }
            if (!r.ok) JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << m_config.name << ": no identity (" << r.error << ")";
            it = asked.emplace(p.identifyCommand(), std::move(r)).first;
        }
        const JPReply& r = it->second;
        answered |= r.ok || !r.lines.empty() || (!r.error.empty() && p.errorIn(r.error));
        if (r.ok && p.identifies(r.lines)) {
            JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << m_config.name << ": identified as " << p.name();
            m_replyProfile = &p;
            m_profile      = &p;
            m_plugins      = p.pluginsIn(r.lines);
            return true;
        }
    }
    // Silence is not a controller. A port that answers nothing is the wrong
    // port, or a board that is off: say so rather than call it connected.
    if (!asked.empty() && !answered) {
        error = m_config.name + ": nothing answered on " + m_link->describe()
              + " (the wrong port, or the controller is off)";
        JLOGC(JPlacerLog::kDriver, JLogLevel::Error) << error;
        return false;
    }
    // Something answered but no profile recognised it: the first profile
    // that does not need to.
    for (const JPFirmwareProfile& p : m_profiles) {
        if (!p.identifyCommand().empty()) continue;
        JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << m_config.name << ": firmware not recognised, using " << p.name();
        m_replyProfile = &p;
        m_profile      = &p;
        return true;
    }
    error = m_config.name + ": the controller did not identify itself as any known firmware";
    return false;
}

void JPGcodeDriver::disconnect() {
    if (m_running) JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << m_config.name << ": disconnecting";
    m_running = false;
    if (m_io.joinable()) m_io.join();
    failAll("disconnected");
    if (m_link) {
        m_link->close();
        m_link.reset();
    }
    m_connected    = false;
    m_replyProfile = nullptr;
    m_profile      = nullptr;
    m_plugins.clear();
}

std::future<JPReply> JPGcodeDriver::failed(const std::string& why) {
    std::promise<JPReply> p;
    JPReply r;
    r.error = why;
    p.set_value(std::move(r));
    return p.get_future();
}

std::future<JPReply> JPGcodeDriver::send(std::string line, int timeoutMs) {
    if (!m_running) return failed(m_config.name + " is not connected");
    Pending p;
    p.line      = std::move(line);
    p.timeoutMs = timeoutMs > 0 ? timeoutMs : m_config.commandTimeoutMs;
    std::future<JPReply> f = p.promise.get_future();
    std::lock_guard lk(m_mutex);
    m_queue.push_back(std::move(p));
    return f;
}

std::future<JPReply> JPGcodeDriver::sendCommand(const std::string& name,
                                                const std::map<std::string, std::string>& values,
                                                int timeoutMs) {
    if (!m_profile) return failed(m_config.name + " is not connected");
    const auto line = m_profile->command(name, values);
    if (!line) return failed(m_profile->name() + " has no '" + name + "' command");
    if (timeoutMs <= 0 && name == "home") timeoutMs = m_config.homeTimeoutMs;
    return send(*line, timeoutMs);
}

bool JPGcodeDriver::readSettings(std::string& error) {
    if (!m_profile || !m_profile->hasSettings()) {
        error = m_config.name + ": its firmware profile does not describe stored settings";
        return false;
    }
    const JPReply r = send(m_profile->settingsReadCommand()).get();
    if (!r.ok) {
        error = m_config.name + ": settings not read (" + r.error + ")";
        JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << error;
        return false;
    }
    std::map<std::string, std::string> read;
    for (const std::string& l : r.lines)
        if (const auto s = m_profile->parseSetting(l)) read[s->first] = s->second;
    JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << m_config.name << ": " << read.size() << " stored setting(s) read";
    for (const auto& [id, value] : read)
        JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << m_config.name << ": setting " << id << " = " << value;
    std::lock_guard lk(m_mutex);
    m_settings = std::move(read);
    return true;
}

std::map<std::string, std::string> JPGcodeDriver::settings() const {
    std::lock_guard lk(m_mutex);
    return m_settings;
}

std::optional<double> JPGcodeDriver::axisSetting(const std::string& key, const std::string& letter) const {
    if (!m_profile) return std::nullopt;
    const auto id = m_profile->axisSettingId(key, letter);
    if (!id) return std::nullopt;
    std::lock_guard lk(m_mutex);
    const auto it = m_settings.find(*id);
    if (it == m_settings.end()) return std::nullopt;
    return std::strtod(it->second.c_str(), nullptr);
}

JPFirmwareProfile::Status JPGcodeDriver::status() const {
    std::lock_guard lk(m_mutex);
    return m_status;
}

void JPGcodeDriver::ioLoop() {
    Clock::time_point nextStatus = Clock::now();
    auto lost = [this](const std::string& why) {
        JLOGC(JPlacerLog::kDriver, JLogLevel::Error) << m_config.name << ": " << why;
        m_running   = false;
        m_connected = false;
        failAll(why);
        onLost.emit(why);
    };

    while (m_running) {
        if (!m_inFlight) {
            std::optional<Pending> next;
            {
                std::lock_guard lk(m_mutex);
                if (!m_queue.empty()) {
                    next.emplace(std::move(m_queue.front()));
                    m_queue.pop_front();
                }
            }
            if (next) {
                m_inFlight  = std::move(next);
                m_collected = {};
                m_deadline  = Clock::now() + std::chrono::milliseconds(m_inFlight->timeoutMs);
                JLOGC(JPlacerLog::kTraffic, JLogLevel::Trace) << m_config.name << " > " << m_inFlight->line;
                onTraffic.emit(true, m_inFlight->line);
                if (!m_link->write(m_inFlight->line + "\n")) {
                    lost("could not write to " + m_link->describe());
                    return;
                }
            }
        }

        // Status reports: a real-time query goes at any moment; a command
        // query only between commands.
        const JPFirmwareProfile* profile = m_connected ? m_profile : nullptr;
        if (profile && !profile->statusCommand().empty() && Clock::now() >= nextStatus) {
            nextStatus = Clock::now() + std::chrono::milliseconds(m_config.statusIntervalMs);
            if (profile->statusIsRealtime()) {
                if (!m_link->write(profile->statusCommand())) {
                    lost("could not write to " + m_link->describe());
                    return;
                }
            } else if (!m_inFlight) {
                Pending p;
                p.line      = profile->statusCommand();
                p.timeoutMs = m_config.commandTimeoutMs;
                std::lock_guard lk(m_mutex);
                if (m_queue.empty()) m_queue.push_back(std::move(p));
            }
        }

        if (const std::optional<std::string> line = m_link->readLine(kReadSliceMs)) handleLine(*line);

        if (m_inFlight && Clock::now() > m_deadline) {
            JPReply r;
            r.lines = std::move(m_collected.lines);
            r.error = "no reply to '" + m_inFlight->line + "' within " + std::to_string(m_inFlight->timeoutMs) + " ms";
            finish(std::move(r));
        }
    }
}

void JPGcodeDriver::handleLine(const std::string& line) {
    const JPFirmwareProfile* p = m_replyProfile.load();
    if (p) {
        if (const auto st = p->parseStatus(line)) {
            {
                std::lock_guard lk(m_mutex);
                m_status = *st;
            }
            JLOGC(JPlacerLog::kStatus, JLogLevel::Trace) << m_config.name << " < " << line;
            onStatus.emit(*st);
            return;
        }
    }
    JLOGC(JPlacerLog::kTraffic, JLogLevel::Trace) << m_config.name << " < " << line;
    onTraffic.emit(false, line);
    if (!p) return;

    if (m_inFlight) {
        if (p->isOk(line)) {
            JPReply r;
            r.ok    = true;
            r.lines = std::move(m_collected.lines);
            finish(std::move(r));
        } else if (const auto e = p->errorIn(line)) {
            JPReply r;
            r.lines = std::move(m_collected.lines);
            r.error = *e;
            finish(std::move(r));
        } else {
            m_collected.lines.push_back(line);
        }
    } else if (const auto e = p->errorIn(line)) {
        JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << m_config.name << ": " << *e;
        onAlarm.emit(*e);
    }
}

void JPGcodeDriver::finish(JPReply reply) {
    if (!reply.ok)
        JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << m_config.name << ": '" << m_inFlight->line << "' failed: " << reply.error;
    m_inFlight->promise.set_value(std::move(reply));
    m_inFlight.reset();
}

void JPGcodeDriver::failAll(const std::string& why) {
    std::deque<Pending> queued;
    {
        std::lock_guard lk(m_mutex);
        queued.swap(m_queue);
    }
    if (m_inFlight) {
        JPReply r;
        r.error = why;
        finish(std::move(r));
    }
    for (Pending& p : queued) {
        JPReply r;
        r.error = why;
        p.promise.set_value(std::move(r));
    }
}

} // inline namespace jf
