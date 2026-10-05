// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPLogFile.h"

#include "JPLogLine.h"

#include <j/core/Log.h>

#include <filesystem>

inline namespace jf {

namespace fs = std::filesystem;

JPLogFile::JPLogFile(std::string path) : m_path(std::move(path)) {
    std::error_code ec;
    fs::create_directories(fs::path(m_path).parent_path(), ec);
    m_bytes = fs::exists(m_path, ec) ? std::size_t(fs::file_size(m_path, ec)) : 0;
    if (m_bytes > kMostBytes) roll();
    else m_out.open(m_path, std::ios::app);
    m_listener = JLog::instance().addListener([this](JLogLevel level, const std::string& category, const std::string& message) {
        write(JPLogLine::text(level, category, message));
    });
}

JPLogFile::~JPLogFile() {
    JLog::instance().removeListener(m_listener);
}

void JPLogFile::write(const std::string& line) {
    std::lock_guard lk(m_mutex);
    if (!m_out) return;
    m_out << line << '\n';
    m_out.flush();
    m_bytes += line.size() + 1;
    if (m_bytes > kMostBytes) roll();
}

void JPLogFile::roll() {
    m_out.close();
    std::error_code ec;
    fs::rename(m_path, m_path + ".1", ec);
    m_out.open(m_path, std::ios::trunc);
    m_bytes = 0;
}

} // inline namespace jf
