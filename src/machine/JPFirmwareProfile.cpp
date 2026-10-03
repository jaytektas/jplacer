// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFirmwareProfile.h"

#include "common/JPlacerLog.h"
#include "common/JPlacerPaths.h"

#include <j/config/Json.h>
#include <j/core/Log.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

inline namespace jf {

namespace {

namespace fs = std::filesystem;

// A pattern from the profile. False with `error` when it is not a valid
// regular expression, so a typo in a profile is reported, not a crash.
bool compile(const JJson& j, const char* key, std::regex& out, std::string& error) {
    const std::string& src = j[key].str();
    try {
        out = std::regex(src, std::regex::ECMAScript | std::regex::optimize);
    } catch (const std::regex_error& e) {
        error = std::string(key) + ": not a valid pattern (" + e.what() + ")";
        return false;
    }
    return true;
}

bool matches(const std::regex& re, const std::string& line) {
    return std::regex_search(line, re);
}

} // namespace

bool JPFirmwareProfile::readReading(const JJson& j, Reading& out, std::string& error) {
    out.id      = j["id"].str();
    out.name    = j["name"].str();
    out.command = j["command"].str();
    out.unit    = j["unit"].str();
    if (out.id.empty() || out.command.empty()) {
        error = "a reading needs an id and a command";
        return false;
    }
    return compile(j, "pattern", out.pattern, error);
}

bool JPFirmwareProfile::load(const std::string& path, std::string& error) {
    const std::optional<JJson> doc = JJson::tryParseFile(path);
    auto fail = [&](const std::string& why) {
        error = path + ": " + why;
        return false;
    };
    if (!doc || !doc->isObject()) return fail("not a JSON object");
    const JJson& j = *doc;

    m_id       = j["id"].str();
    m_name     = j["name"].str();
    m_priority = int(j["priority"].number());
    if (m_id.empty() || m_name.empty()) return fail("a profile needs an id and a name");

    std::string why;
    const JJson& identify = j["identify"];
    m_identifyCommand = identify["command"].str();
    if (!m_identifyCommand.empty() && !compile(identify, "pattern", m_identify, why)) return fail("identify." + why);

    const JJson& replies = j["replies"];
    if (!compile(replies, "ok", m_ok, why) || !compile(replies, "error", m_error, why)
        || !compile(replies, "alarm", m_alarm, why))
        return fail("replies." + why);

    const JJson& status = j["status"];
    m_statusCommand  = status["command"].str();
    m_statusRealtime = status["realtime"].boolean();
    if (!m_statusCommand.empty()) {
        if (!compile(status, "pattern", m_status, why)) return fail("status." + why);
        m_statusStateGroup    = int(status["stateGroup"].number());
        m_statusPositionGroup = int(status["positionGroup"].number());
        m_positionsAsList     = status["positions"].str() != "pairs";
        if (!m_positionsAsList && !compile(status, "pair", m_positionPair, why)) return fail("status." + why);
        m_statusFrameGroup   = int(status["frameGroup"].number());
        m_statusMachineFrame = status["machineFrame"].str();
        m_hasStatusOffset    = !status["offset"].str().empty();
        if (m_hasStatusOffset && !compile(status, "offset", m_statusOffset, why)) return fail("status." + why);
        m_idleState  = status["idleState"].str();
        m_alarmState = status["alarmState"].str();
    }

    const JJson& stop = j["stop"];
    m_feedHold  = stop["feedHold"].str();
    m_holdState = stop["holdState"].str();
    m_reset     = stop["reset"].str();
    m_hasHeld   = !stop["heldPattern"].str().empty();
    if (m_hasHeld && !compile(stop, "heldPattern", m_held, why)) return fail("stop." + why);

    m_axisLetters.clear();
    for (const JJson& l : j["axisLetters"].arr()) m_axisLetters.push_back(l.str());
    if (m_axisLetters.empty()) return fail("axisLetters is empty");
    m_decimals = int(j["decimals"].number());

    m_commands.clear();
    for (const auto& [name, tmpl] : j["commands"].obj()) m_commands[name] = tmpl.str();

    const JJson& settings = j["settings"];
    m_settingsRead = settings["read"].str();
    m_axisSettings.clear();
    if (!m_settingsRead.empty()) {
        if (!compile(settings, "pattern", m_settingLine, why)) return fail("settings." + why);
        m_settingWrite = settings["write"].str();
        for (const JJson& a : settings["axis"].arr())
            m_axisSettings.push_back({ a["key"].str(), int(a["base"].number()), a["unit"].str() });
    }

    m_plugins.clear();
    for (const JJson& pj : j["plugins"].arr()) {
        Plugin p;
        p.name = pj["name"].str();
        if (!compile(pj, "detect", p.detect, why)) return fail("plugin " + p.name + ": " + why);
        for (const JJson& rj : pj["readings"].arr()) {
            Reading r;
            if (!readReading(rj, r, why)) return fail("plugin " + p.name + ": " + why);
            p.readings.push_back(std::move(r));
        }
        const JJson& pass = pj["passThrough"];
        p.passThroughCommand = pass["command"].str();
        if (!p.passThroughCommand.empty() && !compile(pass, "reply", p.passThroughReply, why))
            return fail("plugin " + p.name + " passThrough." + why);
        m_plugins.push_back(std::move(p));
    }
    return true;
}

std::vector<std::string> JPFirmwareProfile::profileDirs() {
    std::vector<std::string> dirs;
    const fs::path exe = JPlacerPaths::exeDir();
    for (const fs::path& d : { exe / "profiles",                 // shipped beside the executable
                               exe / ".." / "profiles" }) {      // build/jplacer -> profiles/
        std::error_code ec;
        if (fs::is_directory(d, ec)) {
            dirs.push_back(fs::weakly_canonical(d, ec).string());
            break;
        }
    }
    if (const std::string cfg = JPlacerPaths::configDir(); !cfg.empty())
        dirs.push_back((fs::path(cfg) / "profiles").string());
    return dirs;
}

std::vector<JPFirmwareProfile> JPFirmwareProfile::loadAll() {
    std::vector<JPFirmwareProfile> out;
    for (const std::string& dir : profileDirs()) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".json") continue;
            JPFirmwareProfile p;
            std::string error;
            if (!p.load(entry.path().string(), error)) {
                JLOGC(JPlacerLog::kProfiles, JLogLevel::Error) << error;
                continue;
            }
            // A user profile with a bundled profile's id replaces it.
            std::erase_if(out, [&](const JPFirmwareProfile& o) { return o.id() == p.id(); });
            JLOGC(JPlacerLog::kProfiles, JLogLevel::Debug) << "profile " << p.id() << " from " << entry.path().string();
            out.push_back(std::move(p));
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const JPFirmwareProfile& a, const JPFirmwareProfile& b) { return a.priority() > b.priority(); });
    return out;
}

bool JPFirmwareProfile::identifies(const std::vector<std::string>& lines) const {
    if (m_identifyCommand.empty()) return false;
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return matches(m_identify, l); });
}

std::vector<const JPFirmwareProfile::Plugin*> JPFirmwareProfile::pluginsIn(const std::vector<std::string>& lines) const {
    std::vector<const Plugin*> found;
    for (const Plugin& p : m_plugins)
        if (std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return matches(p.detect, l); }))
            found.push_back(&p);
    return found;
}

bool JPFirmwareProfile::isOk(const std::string& line) const {
    return std::regex_search(line, m_ok);
}

std::optional<std::string> JPFirmwareProfile::errorIn(const std::string& line) const {
    std::smatch m;
    if (std::regex_search(line, m, m_error) || std::regex_search(line, m, m_alarm)) return line;
    return std::nullopt;
}

std::optional<JPFirmwareProfile::Status> JPFirmwareProfile::parseStatus(const std::string& line) const {
    if (m_statusCommand.empty()) return std::nullopt;
    std::smatch m;
    if (!std::regex_search(line, m, m_status)) return std::nullopt;
    Status st;
    if (m_statusStateGroup > 0 && size_t(m_statusStateGroup) < m.size()) st.state = m[m_statusStateGroup].str();
    st.held = m_hasHeld && std::regex_search(line, m_held);
    if (m_statusFrameGroup > 0 && size_t(m_statusFrameGroup) < m.size())
        st.positionsAreWork = m[m_statusFrameGroup].str() != m_statusMachineFrame;
    if (m_hasStatusOffset) {
        std::smatch o;
        if (std::regex_search(line, o, m_statusOffset) && o.size() > 1) st.offsets = byLetter(o[1].str());
    }
    if (m_positionsAsList) {
        if (m_statusPositionGroup <= 0 || size_t(m_statusPositionGroup) >= m.size()) return st;
        st.positions = byLetter(m[m_statusPositionGroup].str());
    } else {
        for (auto it = std::sregex_iterator(line.begin(), line.end(), m_positionPair); it != std::sregex_iterator(); ++it) {
            const std::string letter = (*it)[1].str();
            if (std::find(m_axisLetters.begin(), m_axisLetters.end(), letter) != m_axisLetters.end())
                st.positions[letter] = std::strtod((*it)[2].str().c_str(), nullptr);
        }
    }
    return st;
}

std::map<std::string, double> JPFirmwareProfile::byLetter(const std::string& list) const {
    std::map<std::string, double> out;
    size_t start = 0;
    for (size_t i = 0; i < m_axisLetters.size() && start <= list.size(); ++i) {
        const size_t comma = list.find(',', start);
        const std::string field = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (field.empty()) break;
        out[m_axisLetters[i]] = std::strtod(field.c_str(), nullptr);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

std::string JPFirmwareProfile::fill(const std::string& tmpl, const std::map<std::string, std::string>& values) {
    std::string out;
    out.reserve(tmpl.size());
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '{') {
            const size_t close = tmpl.find('}', i);
            if (close != std::string::npos) {
                const auto it = values.find(tmpl.substr(i + 1, close - i - 1));
                if (it != values.end()) {
                    out += it->second;
                    i = close;
                    continue;
                }
            }
        }
        out += tmpl[i];
    }
    return out;
}

std::optional<std::string> JPFirmwareProfile::command(const std::string& name,
                                                      const std::map<std::string, std::string>& values) const {
    const auto it = m_commands.find(name);
    if (it == m_commands.end()) return std::nullopt;
    return fill(it->second, values);
}

std::optional<std::pair<std::string, std::string>> JPFirmwareProfile::parseSetting(const std::string& line) const {
    if (m_settingsRead.empty()) return std::nullopt;
    std::smatch m;
    if (!std::regex_match(line, m, m_settingLine) || m.size() < 3) return std::nullopt;
    return std::make_pair(m[1].str(), m[2].str());
}

std::string JPFirmwareProfile::settingWriteCommand(const std::string& id, const std::string& value) const {
    return fill(m_settingWrite, { { "id", id }, { "value", value } });
}

std::optional<std::string> JPFirmwareProfile::axisSettingId(const std::string& key, const std::string& letter) const {
    const auto axis = std::find(m_axisLetters.begin(), m_axisLetters.end(), letter);
    if (axis == m_axisLetters.end()) return std::nullopt;
    for (const AxisSetting& s : m_axisSettings)
        if (s.key == key) return std::to_string(s.base + int(axis - m_axisLetters.begin()));
    return std::nullopt;
}

} // inline namespace jf
