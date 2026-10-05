// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPGcodeDriver.h"
#include "JPGcodeCompressor.h"

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
    : m_id(config.id), m_config(std::make_shared<const JPDriverConfig>(std::move(config))), m_profiles(std::move(profiles)) {}

void JPGcodeDriver::setConfig(JPDriverConfig config) {
    m_config.store(std::make_shared<const JPDriverConfig>(std::move(config)));
}

JPGcodeDriver::~JPGcodeDriver() {
    disconnect();
}

bool JPGcodeDriver::connect(std::string& error) {
    if (m_connected) return true;
    m_link = JPLinkFactory::create(cfg()->link, error);
    if (!m_link) return false;
    if (!m_link->open(error)) {
        m_link.reset();
        return false;
    }
    JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << cfg()->name << ": opened " << m_link->describe();
    m_running = true;
    {
        std::lock_guard lk(m_mutex);
        m_lastSent.clear();   // a new connection: everything sent again
    }
    m_io = std::thread(&JPGcodeDriver::ioLoop, this);
    if (cfg()->connectWaitMs > 0) {
        // The I/O thread reads (and logs) whatever the board says meanwhile.
        JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << cfg()->name << ": listening " << cfg()->connectWaitMs
                                                     << " ms before asking anything";
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg()->connectWaitMs));
    }

    if (!identify(error)) {
        disconnect();
        return false;
    }
    m_connected = true;
    m_initPending = false;
    if (const auto init = m_profile->command("init")) {
        // A controller in alarm (reset mid-move, a limit hit) refuses G-code
        // until it is unlocked: it is connected all the same, to be seen and
        // homed, and the start-up command waits for the unlock homing makes.
        std::string why;
        if (readStatusNow(why) && inAlarm()) {
            JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << cfg()->name << ": in alarm ("
                << m_profile->alarmState() << "); its start-up command waits until homing unlocks it";
            m_initPending = true;
        }
    }
    if (const auto init = m_profile->command("init"); init && !m_initPending) {
        const JPReply r = send(*init).get();
        if (!r.ok) {
            error = cfg()->name + ": the start-up command was refused (" + r.error + ")";
            disconnect();
            return false;
        }
    }
    JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << cfg()->name << ": " << m_profile->name()
                                                << ", " << m_plugins.size() << " plugin(s)";
    return true;
}

bool JPGcodeDriver::identify(std::string& error) {
    if (cfg()->profile != "auto") {
        for (const JPFirmwareProfile& p : m_profiles) {
            if (p.id() != cfg()->profile) continue;
            m_replyProfile = &p;
            if (!p.identifyCommand().empty()) {
                const JPReply r = send(p.identifyCommand(), cfg()->identifyTimeoutMs).get();
                if (r.ok) m_plugins = p.pluginsIn(r.lines);
            }
            m_profile = &p;
            return true;
        }
        error = cfg()->name + ": no firmware profile '" + cfg()->profile + "'";
        return false;
    }

    // Ask each way of identifying once, and offer the answer to every profile
    // that asks the same way, highest priority first. A refusal is asked
    // again once, as is one that got no answer: bytes left on the line, or a
    // greeting arriving in the middle of the answer, spoil one exchange, not
    // the next.
    std::map<std::string, JPReply> asked;
    bool answered = false;   // anything at all came back
    for (const JPFirmwareProfile& p : m_profiles) {
        if (p.identifyCommand().empty()) continue;
        auto it = asked.find(p.identifyCommand());
        if (it == asked.end()) {
            m_replyProfile = &p;
            JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << cfg()->name << ": identifying with '"
                                                         << p.identifyCommand() << "' (" << p.name() << " asks so)";
            JPReply r = send(p.identifyCommand(), cfg()->identifyTimeoutMs).get();
            if (!r.ok) {
                JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << cfg()->name << ": " << r.error << ", asking again";
                r = send(p.identifyCommand(), cfg()->identifyTimeoutMs).get();
            }
            if (!r.ok) JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << cfg()->name << ": no identity (" << r.error << ")";
            it = asked.emplace(p.identifyCommand(), std::move(r)).first;
        }
        const JPReply& r = it->second;
        answered |= r.ok || !r.lines.empty() || (!r.error.empty() && p.errorIn(r.error));
        if (r.ok && p.identifies(r.lines)) {
            JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << cfg()->name << ": identified as " << p.name();
            m_replyProfile = &p;
            m_profile      = &p;
            m_plugins      = p.pluginsIn(r.lines);
            return true;
        }
    }
    // Silence is not a controller. A port that answers nothing is the wrong
    // port, or a board that is off: say so rather than call it connected.
    if (!asked.empty() && !answered) {
        error = cfg()->name + ": nothing answered on " + m_link->describe()
              + " (the wrong port, or the controller is off)";
        JLOGC(JPlacerLog::kDriver, JLogLevel::Error) << error;
        return false;
    }
    // Something answered but no profile recognised it: the first profile
    // that does not need to.
    for (const JPFirmwareProfile& p : m_profiles) {
        if (!p.identifyCommand().empty()) continue;
        JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << cfg()->name << ": firmware not recognised, using " << p.name();
        m_replyProfile = &p;
        m_profile      = &p;
        return true;
    }
    error = cfg()->name + ": the controller did not identify itself as any known firmware";
    return false;
}

void JPGcodeDriver::disconnect() {
    if (m_running) JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << cfg()->name << ": disconnecting";
    m_running = false;
    if (m_io.joinable()) m_io.join();
    failAll("disconnected");
    if (m_link) {
        m_link->close();
        m_link.reset();
    }
    m_connected    = false;
    {
        std::lock_guard lk(m_mutex);
        m_offsets.clear();
    }
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
    if (!m_running) return failed(cfg()->name + " is not connected");
    Pending p;
    p.line      = std::move(line);
    p.timeoutMs = timeoutMs > 0 ? timeoutMs : cfg()->commandTimeoutMs;
    std::future<JPReply> f = p.promise.get_future();
    std::lock_guard lk(m_mutex);
    m_queue.push_back(std::move(p));
    return f;
}

JPReply JPGcodeDriver::command(const std::string& name, const std::map<std::string, std::string>& values,
                               int timeoutMs) {
    const auto c = cfg();   // one set of settings for the whole command
    if (!m_profile) return failed(c->name + " is not connected").get();
    // A move's feed, acceleration and jerk left out when sent already (On Change Only).
    std::map<std::string, std::string> v = values;
    if (name == "move") {
        std::lock_guard lk(m_mutex);
        for (const auto& [key, s] : { std::pair { "feed", c->sendOnChangeFeed }, std::pair { "acceleration", c->sendOnChangeAcceleration },
                                      std::pair { "jerk", c->sendOnChangeJerk } }) {
            const auto it = v.find(key);
            if (it == v.end()) continue;
            const double now = std::strtod(it->second.c_str(), nullptr);
            const auto last = m_lastSent.find(key);
            if (s.on && now != 0 && last != m_lastSent.end() && std::abs(now - last->second) <= s.relativeDeviation * std::abs(last->second))
                it->second = JPFirmwareProfile::kLeaveOut;
            else
                m_lastSent[key] = now;
        }
    }
    // Homing (or connecting) starts afresh: everything sent again.
    if (name == "home") {
        std::lock_guard lk(m_mutex);
        m_lastSent.clear();
    }
    const auto own  = c->commands.find(name);
    const auto text = own != c->commands.end()
                    ? std::optional<std::string>(JPFirmwareProfile::fill(own->second, v))
                    : m_profile->command(name, v);
    if (!text) return failed(m_profile->name() + " has no '" + name + "' command").get();
    if (timeoutMs <= 0 && name == "home") timeoutMs = c->homeTimeoutMs;
    return sendLines(*text, timeoutMs);
}

JPReply JPGcodeDriver::sendLines(const std::string& text, int timeoutMs) {
    JPReply all;
    all.ok = true;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t eol = text.find('\n', start);
        std::string line = text.substr(start, eol == std::string::npos ? std::string::npos : eol - start);
        start = eol == std::string::npos ? text.size() + 1 : eol + 1;
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        JPReply r = send(std::move(line), timeoutMs).get();
        all.lines.insert(all.lines.end(), r.lines.begin(), r.lines.end());
        if (!r.ok) {
            all.ok    = false;
            all.error = r.error;
            break;
        }
    }
    return all;
}

JPReply JPGcodeDriver::waitForMotion() {
    JPReply r = command("waitMotion", {}, cfg()->homeTimeoutMs);
    if (!r.ok || !m_profile || m_profile->statusCommand().empty()) return r;
    // A report read after the answer was made after the motion ended.
    std::string why;
    if (!readStatusNow(why)) {
        r.ok    = false;
        r.error = why + " of the motion ending";
    }
    return r;
}

bool JPGcodeDriver::readStatusNow(std::string& error) {
    if (!m_profile || m_profile->statusCommand().empty()) {
        error = "the firmware profile has no status report";
        return false;
    }
    std::unique_lock lk(m_mutex);
    const uint64_t after = m_statusCount;
    m_statusNow = true;
    if (!m_statusRead.wait_for(lk, std::chrono::milliseconds(cfg()->commandTimeoutMs),
                               [&] { return m_statusCount > after || !m_connected; })) {
        error = "no status report within " + std::to_string(cfg()->commandTimeoutMs) + " ms";
        return false;
    }
    if (!m_connected) {
        error = "the connection was lost";
        return false;
    }
    return true;
}

bool JPGcodeDriver::inAlarm() const {
    const JPFirmwareProfile* p = m_profile;
    if (!p || p->alarmState().empty()) return false;
    std::lock_guard lk(m_mutex);
    return m_status.state == p->alarmState();
}

JPReply JPGcodeDriver::unlockForHoming() {
    JPReply ok;
    ok.ok = true;
    std::string why;
    if (!m_initPending && !(readStatusNow(why) && inAlarm())) return ok;
    if (inAlarm()) {
        JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << cfg()->name << ": unlocking the alarm to home";
        const JPReply r = command("unlock");
        if (!r.ok) return r;
    }
    if (m_initPending) {
        if (const auto init = m_profile->command("init")) {
            const JPReply r = send(*init).get();
            if (!r.ok) return r;
        }
        m_initPending = false;
    }
    return ok;
}

bool JPGcodeDriver::halt(bool emergency) {
    const JPFirmwareProfile* p = m_profile;
    if (!m_connected || !p || p->reset().empty()) return false;
    m_halt = emergency ? 2 : 1;
    return true;
}

bool JPGcodeDriver::readSettings(std::string& error) {
    if (!m_profile || !m_profile->hasSettings()) {
        error = cfg()->name + ": its firmware profile does not describe stored settings";
        return false;
    }
    const JPReply r = send(m_profile->settingsReadCommand()).get();
    if (!r.ok) {
        error = cfg()->name + ": settings not read (" + r.error + ")";
        JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << error;
        return false;
    }
    std::map<std::string, std::string> read;
    for (const std::string& l : r.lines)
        if (const auto s = m_profile->parseSetting(l)) read[s->first] = s->second;
    JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << cfg()->name << ": " << read.size() << " stored setting(s) read";
    for (const auto& [id, value] : read)
        JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << cfg()->name << ": setting " << id << " = " << value;
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
        JLOGC(JPlacerLog::kDriver, JLogLevel::Error) << cfg()->name << ": " << why;
        m_running   = false;
        m_connected = false;
        failAll(why);
        onLost.emit(why);
    };

    // Throws the controller's queue away (the firmware's reset) and fails
    // what waits on it.
    auto resetNow = [&](const std::string& why) {
        const JPFirmwareProfile* p = m_profile;
        if (!m_link->write(p->reset())) {
            lost("could not write to " + m_link->describe());
            return false;
        }
        JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << cfg()->name << ": " << why;
        m_holding = false;
        failAll(why);
        return true;
    };

    while (m_running) {
        // A stop asked for: before anything else is sent.
        if (const int h = m_halt.exchange(0); h != 0 && m_profile) {
            const JPFirmwareProfile* p = m_profile;
            if (h == 2 || p->feedHold().empty()) {
                if (!resetNow(h == 2 ? "emergency stop" : "stopped")) return;
            } else {
                if (!m_link->write(p->feedHold())) {
                    lost("could not write to " + m_link->describe());
                    return;
                }
                JLOGC(JPlacerLog::kDriver, JLogLevel::Info) << cfg()->name << ": feed hold";
                m_holding = true;
                m_holdLast.clear();
                m_holdUntil = Clock::now() + std::chrono::milliseconds(cfg()->commandTimeoutMs);
                m_statusNow = true;
            }
        }
        // Held: once the reports say the hold has completed (else, where the
        // profile cannot tell, held with the axes still from one report to
        // the next), the queue is thrown away and the position stays. Not
        // held in time: reset anyway, and the controller may have lost its
        // place (onPlaceLost).
        if (m_holding) {
            JPFirmwareProfile::Status st;
            {
                std::lock_guard lk(m_mutex);
                st = m_status;
            }
            const bool held = !m_profile->holdState().empty() && st.state == m_profile->holdState();
            const bool still = m_profile->knowsHeld() ? st.held : held && !m_holdLast.empty() && st.positions == m_holdLast;
            if (still) {
                if (!resetNow("stopped")) return;
            } else if (Clock::now() > m_holdUntil) {
                if (!resetNow("stopped before it held still")) return;
                onPlaceLost.emit("it did not hold still within " + std::to_string(cfg()->commandTimeoutMs) + " ms of the stop");
            } else if (held) {
                m_holdLast = st.positions;
            }
        }
        if (!m_inFlight && !m_holding) {
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
                // As the controller's settings say: comments removed, compressed, escapes made characters.
                m_inFlight->line = JPGcodeCompressor::process(m_inFlight->line, cfg()->compression());
                m_collected = {};
                m_deadline  = Clock::now() + std::chrono::milliseconds(m_inFlight->timeoutMs);
                JLOGC(JPlacerLog::kTraffic, cfg()->logGcode ? JLogLevel::Info : JLogLevel::Trace) << cfg()->name << " > " << m_inFlight->line;
                onTraffic.emit(true, m_inFlight->line);
                if (!m_link->write(m_inFlight->line + lineEnding())) {
                    lost("could not write to " + m_link->describe());
                    return;
                }
            }
        }

        // Status reports: a real-time query goes at any moment; a command
        // query only between commands.
        const JPFirmwareProfile* profile = m_connected ? m_profile : nullptr;
        if (m_statusNow.exchange(false)) nextStatus = Clock::now();
        if (profile && !profile->statusCommand().empty() && Clock::now() >= nextStatus) {
            nextStatus = Clock::now() + std::chrono::milliseconds(cfg()->statusIntervalMs);
            if (profile->statusIsRealtime()) {
                if (!m_link->write(profile->statusCommand())) {
                    lost("could not write to " + m_link->describe());
                    return;
                }
            } else if (!m_inFlight) {
                Pending p;
                p.line      = profile->statusCommand();
                p.timeoutMs = cfg()->commandTimeoutMs;
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
        if (auto st = p->parseStatus(line)) {
            {
                std::lock_guard lk(m_mutex);
                if (!st->offsets.empty()) m_offsets = st->offsets;
                if (!st->positionsAreWork) {
                    for (auto& [letter, v] : st->positions)
                        if (const auto o = m_offsets.find(letter); o != m_offsets.end()) v -= o->second;
                    st->positionsAreWork = true;
                }
                m_status = *st;
            }
            JLOGC(JPlacerLog::kStatus, JLogLevel::Trace) << cfg()->name << " < " << line;
            onStatus.emit(*st);
            // Counted after it has been passed on, so whoever waits for it
            // sees what the report changed.
            {
                std::lock_guard lk(m_mutex);
                ++m_statusCount;
            }
            m_statusRead.notify_all();
            return;
        }
    }
    JLOGC(JPlacerLog::kTraffic, cfg()->logGcode ? JLogLevel::Info : JLogLevel::Trace) << cfg()->name << " < " << line;
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
        JLOGC(JPlacerLog::kDriver, JLogLevel::Warn) << cfg()->name << ": " << *e;
        onAlarm.emit(*e);
    }
}

void JPGcodeDriver::finish(JPReply reply) {
    if (!reply.ok)
        JLOGC(JPlacerLog::kDriver, JLogLevel::Debug) << cfg()->name << ": '" << m_inFlight->line << "' failed: " << reply.error;
    m_inFlight->promise.set_value(std::move(reply));
    m_inFlight.reset();
}

void JPGcodeDriver::failAll(const std::string& why) {
    std::deque<Pending> queued;
    {
        std::lock_guard lk(m_mutex);
        queued.swap(m_queue);
    }
    m_statusRead.notify_all();   // a wait for a report after motion ends here too
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


std::string JPGcodeDriver::lineEnding() const {
    const std::string& e = std::as_const(cfg()->link)["lineEnding"].str();
    return e == "CR" ? "\r" : e == "CRLF" ? "\r\n" : "\n";
}

} // inline namespace jf
