// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <cstddef>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

inline namespace jf {

// The log written to a file as it comes (as OpenPnP's log/OpenPnP.log), for
// Submit Diagnostics and for looking back: each entry a line (JPLogLine);
// grown past kMostBytes, it is moved aside to the same name and ".1" (the one
// before that dropped) and begun again.
class JPLogFile {
public:
    static constexpr std::size_t kMostBytes = 10u << 20;

    // `path`: the file, its folder made if need be.
    explicit JPLogFile(std::string path);
    ~JPLogFile();
    JPLogFile(const JPLogFile&) = delete;
    JPLogFile& operator=(const JPLogFile&) = delete;

    const std::string& path() const { return m_path; }

private:
    void write(const std::string& line);
    void roll();

    std::string   m_path;
    std::mutex    m_mutex;
    std::ofstream m_out;
    std::size_t   m_bytes = 0;
    int           m_listener = 0;
};

} // inline namespace jf
