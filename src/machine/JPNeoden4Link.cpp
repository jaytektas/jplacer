// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPNeoden4Link.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <sstream>
#include <thread>

inline namespace jf {

namespace {

// The machine's bytes through a claimed serial port.
class SerialBytes : public JPNeoden4Protocol::Port {
public:
    explicit SerialBytes(JPSerialLink& serial) : m_serial(serial) {}

    bool write(uint8_t byte) override {
        return m_serial.write(std::string(1, char(byte)));
    }
    std::optional<uint8_t> read(int timeoutMs) override {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
        while (m_in.empty()) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
            if (left <= 0) return std::nullopt;
            const std::vector<uint8_t> chunk = m_serial.readBytes(int(left));
            m_in.insert(m_in.end(), chunk.begin(), chunk.end());
        }
        const uint8_t b = m_in.front();
        m_in.pop_front();
        return b;
    }
    // As OpenPnP's flushInput: read until nothing more comes.
    void flushInput() override {
        m_in.clear();
        while (!m_serial.readBytes(JPNeoden4Link::kFlushSliceMs).empty()) {}
    }

private:
    JPSerialLink&       m_serial;
    std::deque<uint8_t> m_in;
};

std::string upper(std::string s) {
    for (char& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

std::vector<std::string> wordsOf(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    return words;
}

std::optional<double> numberOf(const std::string& s) {
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return std::nullopt;
    return v;
}

// A G-code line's words after its first, as letter and number ("X10.5").
bool lettered(const std::vector<std::string>& words, std::map<std::string, double>& out, std::string& why) {
    for (size_t i = 1; i < words.size(); ++i) {
        const std::string& w = words[i];
        const auto v = numberOf(w.substr(1));
        if (w.size() < 2 || !std::isalpha(static_cast<unsigned char>(w[0])) || !v) {
            why = "'" + w + "' is not a letter and a number";
            return false;
        }
        out[w.substr(0, 1)] = *v;
    }
    return true;
}

// The index (1 to 4) of `letter` in `letters`; 0 when not one of them.
int indexIn(const std::string& letters, const std::string& letter) {
    const size_t at = letter.size() == 1 ? letters.find(letter[0]) : std::string::npos;
    return at == std::string::npos ? 0 : int(at) + 1;
}

} // namespace

JPNeoden4Link::JPNeoden4Link(Settings settings)
    : m_settings(std::move(settings)), m_serial(std::make_unique<JPSerialLink>(m_settings.serial)),
      m_timeoutMs(JPNeoden4Protocol::kTimeoutMs) {
    m_serialBytes = std::make_unique<SerialBytes>(*m_serial);
    m_port = m_serialBytes.get();
}

JPNeoden4Link::JPNeoden4Link(Settings settings, JPNeoden4Protocol::Port& port, int timeoutMs)
    : m_settings(std::move(settings)), m_port(&port), m_timeoutMs(timeoutMs) {}

JPNeoden4Link::~JPNeoden4Link() {
    close();
}

bool JPNeoden4Link::open(std::string& error) {
    if (m_serial && !m_serial->open(error)) return false;
    m_protocol = std::make_unique<JPNeoden4Protocol>(*m_port, m_timeoutMs);
    m_open = true;
    m_partial.clear();
    m_replies.clear();
    m_homed = m_justHomed = false;
    m_offsets.clear();
    m_at.clear();
    m_feedPerMin = 0;
    return true;
}

void JPNeoden4Link::close() {
    if (m_serial) m_serial->close();
    m_protocol.reset();
    m_open = false;
}

bool JPNeoden4Link::isOpen() const {
    return m_open;
}

bool JPNeoden4Link::write(const std::string& bytes) {
    if (!m_open) return false;
    m_partial += bytes;
    size_t eol;
    while ((eol = m_partial.find_first_of("\r\n")) != std::string::npos) {
        const std::string line = m_partial.substr(0, eol);
        m_partial.erase(0, eol + 1);
        if (line.find_first_not_of(" \t") != std::string::npos) run(line);
    }
    return true;
}

std::optional<std::string> JPNeoden4Link::readLine(int timeoutMs) {
    // Every line is answered inside write(): an empty queue stays empty for the whole wait.
    if (m_replies.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return std::nullopt;
    }
    std::string line = std::move(m_replies.front());
    m_replies.pop_front();
    return line;
}

std::string JPNeoden4Link::describe() const {
    return m_serial ? "NeoDen 4 on " + m_serial->describe() : "NeoDen 4";
}

void JPNeoden4Link::run(const std::string& line) {
    const std::vector<std::string> words = wordsOf(upper(line));
    const std::string& what = words.front();
    std::string why;
    auto args = [&](size_t n, std::vector<double>& out) {
        if (words.size() != n + 1) {
            why = what + " takes " + std::to_string(n) + " number" + (n == 1 ? "" : "s");
            return false;
        }
        for (size_t i = 1; i < words.size(); ++i) {
            const auto v = numberOf(words[i]);
            if (!v) {
                why = "'" + words[i] + "' is not a number";
                return false;
            }
            out.push_back(*v);
        }
        return true;
    };
    auto onOff = [&](size_t at, bool& on) {
        if (words.size() != at + 1 || (words[at] != "ON" && words[at] != "OFF")) {
            why = what + " takes ON or OFF";
            return false;
        }
        on = words[at] == "ON";
        return true;
    };
    auto nozzleOf = [&](const std::string& w, int& n) {
        const auto v = numberOf(w);
        n = v ? int(*v) : 0;
        if (n < 1 || n > 4) {
            why = "'" + w + "' is not a nozzle (1 to 4)";
            return false;
        }
        return true;
    };
    bool ok = false;
    std::vector<double> a;
    std::map<std::string, double> to;
    bool on = false;
    int n = 0;
    if (what == "M115") {
        m_replies.push_back("FIRMWARE_NAME:NeoDen4");
        ok = true;
    } else if (what == "?") {
        m_replies.push_back(status());
        ok = true;
    } else if (what == "M400") {
        ok = true;
    } else if (what == "G28") {
        ok = home(why);
    } else if (what == "G92") {
        ok = lettered(words, to, why) && setPositions(to, why);
    } else if (what == "G0" || what == "G1") {
        ok = lettered(words, to, why);
        std::optional<double> feed;
        if (ok && what == "G1") {
            if (const auto f = to.find("F"); f != to.end()) {
                m_feedPerMin = f->second;
                to.erase(f);
            }
            feed = m_feedPerMin;
        }
        to.erase("F");
        ok = ok && move(to, feed, why);
    } else if (what == "G4") {
        ok = lettered(words, to, why);
        if (ok) pause(int(to.count("P") ? to["P"] : 0));
    } else if (what == "VACUUM") {
        ok = words.size() == 3 && nozzleOf(words[1], n) && onOff(2, on) && vacuum(n, on, why);
        if (words.size() != 3 && why.empty()) why = "VACUUM takes a nozzle and ON or OFF";
    } else if (what == "AIR") {
        ok = words.size() == 3 && nozzleOf(words[1], n) && args(2, a) && m_protocol->setAir(n, int(a[1]), why);
        if (words.size() != 3 && why.empty()) why = "AIR takes a nozzle and a value";
    } else if (what == "AIR?") {
        int value = 0;
        ok = words.size() == 2 && nozzleOf(words[1], n) && m_protocol->readAir(n, value, why);
        if (ok) m_replies.push_back("AIR:" + std::to_string(value));
        if (words.size() != 2 && why.empty()) why = "AIR? takes a nozzle";
    } else if (what == "LIGHTS") {
        const bool down = words.size() > 1 && words[1] == "DOWN", up = words.size() > 1 && words[1] == "UP";
        if (!down && !up) why = "LIGHTS takes DOWN or UP and a level";
        else if (words.size() == 3 && numberOf(words[2])) {
            const int level = int(*numberOf(words[2]));
            ok = down ? m_protocol->lightsDown(level, why) : m_protocol->lightsUp(level, why);
        } else why = "LIGHTS takes DOWN or UP and a level";
    } else if (what == "RAILS") {
        ok = args(1, a) && m_protocol->rails(int(a[0]), why);
    } else if (what == "RELEASEC") {
        ok = m_protocol->moveC(0, 0, why);
    } else if (what == "BUZZER") {
        ok = onOff(1, on) && m_protocol->buzzer(on, why);
    } else if (what == "FEED" || what == "PEEL") {
        ok = args(3, a) && (what == "FEED" ? m_protocol->feed(int(a[0]), int(a[1]), int(a[2]), why)
                                           : m_protocol->peel(int(a[0]), int(a[1]), int(a[2]), why));
    } else if (what == "FEEDERID") {
        ok = args(2, a) && m_protocol->changeFeederId(int(a[0]), int(a[1]), why);
    } else if (what == "NEOFEED") {
        ok = args(6, a) && neoFeed(a, why);
    } else {
        why = "'" + what + "' is not a NeoDen 4 command";
    }
    if (ok) {
        m_replies.push_back("ok");
    } else {
        JLOGC(JPlacerLog::kLink, JLogLevel::Warn) << describe() << ": '" << line << "': " << why;
        m_replies.push_back("error: " + why);
    }
}

bool JPNeoden4Link::home(std::string& why) {
    // OpenPnP's: the offsets forgotten, and the machine homed only the first time.
    m_offsets.clear();
    m_justHomed = true;
    if (m_homed) return true;
    if (!m_protocol->home(why)) {
        m_justHomed = false;
        return false;
    }
    m_homed = true;
    return true;
}

bool JPNeoden4Link::setPositions(const std::map<std::string, double>& to, std::string& why) {
    for (const auto& [letter, v] : to)
        if (std::find(kLetters.begin(), kLetters.end(), letter) == kLetters.end()) {
            why = letter + " is not one of its axes";
            return false;
        }
    // OpenPnP's setGlobalOffsets: X and Y moved by the difference (homing's
    // coordinates are only where the axes are); Z and rotation are sent as they are.
    for (const auto& [letter, v] : to) {
        if (!m_justHomed && (letter == "X" || letter == "Y")) m_offsets[letter] += v - m_at[letter];
        m_at[letter] = v;
    }
    m_justHomed = false;
    return true;
}

bool JPNeoden4Link::move(const std::map<std::string, double>& to, std::optional<double> feedPerMin, std::string& why) {
    if (!m_homed) {
        why = "NeoDen4Driver moveTo: Machine must be homed before movement";
        return false;
    }
    for (const auto& [letter, v] : to)
        if (std::find(kLetters.begin(), kLetters.end(), letter) == kLetters.end()) {
            why = letter + " is not one of its axes";
            return false;
        }
    // OpenPnP's speed: the feed rate over its 250 mm/s (G0, or none given: full).
    const double speed = feedPerMin && *feedPerMin > 0 ? std::clamp(*feedPerMin / 60 / kFastestMmPerS, 0.0, 1.0) : 1.0;
    for (int t = 0; t < kMoveTries; ++t) {
        if (moveOnce(to, speed, why)) {
            for (const auto& [letter, v] : to) m_at[letter] = v;
            m_justHomed = false;
            return true;
        }
        pause(kRetryPauseMs);
        m_port->flushInput();
        pause(kRetryPauseMs);
        JLOGC(JPlacerLog::kLink, JLogLevel::Warn) << describe() << ": Recovered moveTo (" << why << ")";
    }
    why = "MoveTo error. (" + why + ")";
    return false;
}

bool JPNeoden4Link::moveOnce(const std::map<std::string, double>& to, double speed, std::string& why) {
    auto changes = [&](const std::string& letter) {
        const auto it = to.find(letter);
        if (it == to.end()) return false;
        const auto at = m_at.find(letter);
        return at == m_at.end() || at->second != it->second;
    };
    // The rotations, then the Zs, that change, each kind followed by its pause.
    for (const auto& [letters, isRotation] : { std::pair { std::string("ABCD"), true }, std::pair { std::string("ZUVW"), false } }) {
        bool moved = false;
        for (const auto& [letter, v] : to) {
            const int nozzle = indexIn(letters, letter);
            if (nozzle == 0 || !changes(letter)) continue;
            if (!(isRotation ? m_protocol->moveC(nozzle, v, why) : m_protocol->moveZ(nozzle, v, why))) return false;
            moved = true;
        }
        if (moved) pause(kSettleMs);
    }
    if (changes("X") || changes("Y")) {
        const double x = to.count("X") ? to.at("X") : m_at["X"], y = to.count("Y") ? to.at("Y") : m_at["Y"];
        if (!m_protocol->setMoveSpeed(speed, why)) return false;
        const auto steps = [](double mm, double scale) { return int32_t(mm * scale * 100); };
        if (!m_protocol->moveSteps(steps(x - m_offsets["X"], m_settings.scaleX), steps(y - m_offsets["Y"], m_settings.scaleY), why))
            return false;
        pause(kSettleMs);
    }
    return true;
}

bool JPNeoden4Link::vacuum(int nozzle, bool on, std::string& why) {
    if (on) return m_protocol->setAir(nozzle, kVacuumOn, why);
    // OpenPnP's: a little air to let go, then none.
    if (!m_protocol->setAir(nozzle, kVacuumRelease, why)) return false;
    pause(kSettleMs);
    return m_protocol->setAir(nozzle, 0, why);
}

bool JPNeoden4Link::neoFeed(const std::vector<double>& a, std::string& why) {
    const double length = a[5];
    if (length <= 0) {
        why = "Actuation feedLength can't be lower than 0!";
        return false;
    }
    return m_protocol->feed(int(a[0]), int(a[1]), int(length), why)
        && m_protocol->peel(int(a[2]), int(a[3]), int((a[4] / 100.0) * 5 * length), why);
}

void JPNeoden4Link::pause(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string JPNeoden4Link::status() const {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(4);
    out << "<Idle|Pos:";
    for (size_t i = 0; i < kLetters.size(); ++i) {
        const auto at = m_at.find(kLetters[i]);
        out << (i ? "," : "") << (at == m_at.end() ? 0.0 : at->second);
    }
    out << ">";
    return out.str();
}

} // inline namespace jf
