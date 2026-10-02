// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>

inline namespace jf {

class JJson;

// What jplacer knows about one controller firmware, as data: how to recognise
// it, the commands it takes, how to read its replies and status reports, and
// how to read and write the settings the controller stores itself.
//
// Profiles are JSON files (profiles/*.json beside the executable, plus any in
// the user's configuration directory), so supporting another firmware, version
// or plugin is a description, not code. JPGcodeDriver is the only consumer.
class JPFirmwareProfile {
public:
    // A value the firmware can report on request: a command to send (a
    // template; e.g. {input} for which analog input) and the pattern whose
    // first group is the number in its reply.
    struct Reading {
        std::string id;
        std::string name;
        std::string command;
        std::regex  pattern;
        std::string unit;
    };

    // An extension recognised in the identification reply (e.g. a grblHAL
    // plugin), and what it adds: readings, and a bus pass-through (a command
    // that carries a packet to a bus such as RS-485 and returns the reply;
    // `passThroughCommand` has a {payload} placeholder, the first group of
    // `passThroughReply` is the reply's payload).
    struct Plugin {
        std::string          name;
        std::regex           detect;
        std::vector<Reading> readings;
        std::string          passThroughCommand;
        std::regex           passThroughReply;
    };

    // A per-axis setting the controller stores, numbered `base` + the axis's
    // index in axisLetters() (Grbl: steps/mm $100, $101, ...).
    struct AxisSetting {
        std::string key;     // stepsPerMm, maxRate, acceleration, travel
        int         base = 0;
        std::string unit;
    };

    // Read `path`. False with `error` naming the file and the problem.
    bool load(const std::string& path, std::string& error);

    // Every profile in the bundled and user profile directories, highest
    // priority first (the order identification is tried in). Files that do
    // not load are logged and left out.
    static std::vector<JPFirmwareProfile> loadAll();

    // The directories loadAll() reads, bundled first.
    static std::vector<std::string> profileDirs();

    const std::string& id()   const { return m_id; }
    const std::string& name() const { return m_name; }
    int priority() const { return m_priority; }

    // Identification: the command that makes the controller say what it is,
    // and whether `lines` (its reply) are this firmware.
    const std::string& identifyCommand() const { return m_identifyCommand; }
    bool identifies(const std::vector<std::string>& lines) const;
    std::vector<const Plugin*> pluginsIn(const std::vector<std::string>& lines) const;

    bool isOk(const std::string& line) const;
    // The error text when `line` reports a failed command or an alarm.
    std::optional<std::string> errorIn(const std::string& line) const;

    // Status reports.
    const std::string& statusCommand() const { return m_statusCommand; }
    bool statusIsRealtime() const { return m_statusRealtime; }
    // Parse a status line: the controller state and the axis positions it
    // reports, by axis letter. Nothing if `line` is not a status report.
    struct Status {
        std::string                   state;
        std::map<std::string, double> positions;
    };
    std::optional<Status> parseStatus(const std::string& line) const;

    const std::vector<std::string>& axisLetters() const { return m_axisLetters; }
    int decimals() const { return m_decimals; }

    // A named command template with its {placeholders} filled in. Nothing
    // when this firmware has no such command.
    std::optional<std::string> command(const std::string& name,
                                       const std::map<std::string, std::string>& values = {}) const;

    // Settings the controller stores.
    bool hasSettings() const { return !m_settingsRead.empty(); }
    const std::string& settingsReadCommand() const { return m_settingsRead; }
    // A settings line as (setting id, value); nothing if it is not one.
    std::optional<std::pair<std::string, std::string>> parseSetting(const std::string& line) const;
    std::string settingWriteCommand(const std::string& id, const std::string& value) const;
    const std::vector<AxisSetting>& axisSettings() const { return m_axisSettings; }
    // The setting id for `key` on the axis `letter`; nothing if not stored.
    std::optional<std::string> axisSettingId(const std::string& key, const std::string& letter) const;

    // A command template with its {placeholders} filled in.
    static std::string fill(const std::string& tmpl, const std::map<std::string, std::string>& values);

private:
    static bool readReading(const JJson& j, Reading& out, std::string& error);

    std::string m_id;
    std::string m_name;
    int         m_priority = 0;

    std::string m_identifyCommand;
    std::regex  m_identify;
    std::vector<Plugin> m_plugins;

    std::regex m_ok;
    std::regex m_error;
    std::regex m_alarm;

    std::string m_statusCommand;
    bool        m_statusRealtime = false;
    std::regex  m_status;
    int         m_statusStateGroup = 0;
    int         m_statusPositionGroup = 0;
    // Positions as a comma list in axis-letter order (Grbl), or as
    // letter:value pairs found anywhere in the line (Marlin M114).
    bool        m_positionsAsList = true;
    std::regex  m_positionPair;

    std::vector<std::string> m_axisLetters;
    int m_decimals = 0;
    std::map<std::string, std::string> m_commands;

    std::string m_settingsRead;
    std::regex  m_settingLine;
    std::string m_settingWrite;
    std::vector<AxisSetting> m_axisSettings;
};

} // inline namespace jf
