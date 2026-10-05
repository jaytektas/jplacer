// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerDiagnosticsDialog.h"

#include "common/JPlacerPaths.h"
#include "ui/JPUiParts.h"

#include <j/core/JStyle.h>

#include <sys/sysinfo.h>
#include <sys/utsname.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

inline namespace jf {

namespace fs = std::filesystem;

namespace {

std::string read(const std::string& path, size_t most) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto size = uint64_t(std::max<std::streamoff>(0, in.tellg()));
    const std::streamoff from = size > most ? std::streamoff(size - most) : 0;
    in.seekg(from);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return from > 0 ? "(the last " + std::to_string(most / 1024) + " KB)\n" + text : text;
}

} // namespace

JPlacerDiagnosticsDialog::JPlacerDiagnosticsDialog(Sources sources,
                                                   std::function<void(const std::string&, const std::string&)> written,
                                                   JGpuHal& hal, int sx, int sy, NativeWinHandleType parent)
    : JDialogWindow("Submit Diagnostics", kW, kH, hal, sx, sy, parent), m_sources(std::move(sources)), m_written(std::move(written)) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    m_page = std::make_unique<JContainer>(g, 0.f, 0.f);
    JPUiParts::asPanel(*m_page);
    auto note = [&](const std::string& text) {
        auto l = std::make_unique<JLabel>(g, text, 0.f);
        l->setWordWrap(true);
        m_notes.push_back(l.get());
        m_page->add(std::move(l));
    };
    note("Describe the problem you are experiencing below, select the checkboxes to include content that will help the "
         "developers resolve your issue, then click Send.");
    note("jplacer sends nothing anywhere: the diagnostics are written to a file in the diagnostics folder, which is "
         "opened, for you to attach to an issue (github.com/jaytektas/jplacer/issues) or wherever you choose. Be aware "
         "that whoever you share it with sees what it holds, so you should not include private or proprietary information.");
    m_page->add(std::make_unique<JLabel>(g, "Please Describe The Issue", 0.f));
    auto description = std::make_unique<JTextArea>(g);
    description->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    m_description = description.get();
    m_page->add(std::move(description));
    m_page->add(std::make_unique<JLabel>(g, "Include", 0.f));
    auto check = [&](const std::string& label, bool on) {
        auto c = std::make_unique<JCheckBox>(g, label, 0.f);
        c->setChecked(on);
        JCheckBox* p = c.get();
        m_page->add(std::move(c));
        return p;
    };
    m_cell = check("Machine (its cell file)", true);
    m_parts = check("parts.xml", true);
    m_packages = check("packages.xml", true);
    m_log = check("Latest Log File", true);
    m_system = check("Anonymous System Information", true);
    m_job = check("Current Job Data (Job Will Be Saved First)", false);
    add(m_page.get());

    m_buttons = std::make_unique<JDialogButtonBox>(graph());
    m_buttons->addButton("Send", JDialogButtonBox::Role::Accept);
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject);
    m_buttons->onAccept.connect([this] { send(); });
    m_buttons->onReject.connect([this] { close(); });
    add(m_buttons.get());
}

std::string JPlacerDiagnosticsDialog::compose(const std::string& description, const std::string& systemInfo,
                                              const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out = "jplacer Diagnostics\n\nDescription: " + description + "\n\n";
    if (!systemInfo.empty()) out += "System:\n\n" + systemInfo + "\n\n";
    out += "Files:\n\n";
    for (const auto& [name, path] : files)
        out += "**** " + name + " ****\n\n" + read(path, name == "Latest Log File" ? kMostLogBytes : SIZE_MAX) + "\n\n";
    return out;
}

std::string JPlacerDiagnosticsDialog::systemInfo(const std::string& version) {
    std::ostringstream s;
    utsname u {};
    if (::uname(&u) == 0) s << "os.name: " << u.sysname << "\nos.version: " << u.release << "\nos.arch: " << u.machine << "\n";
    struct sysinfo i {};
    if (::sysinfo(&i) == 0) {
        char mem[80];
        std::snprintf(mem, sizeof mem, "Memory Total: %.2f\nMemory Free: %.2f\n", double(i.totalram) * i.mem_unit / 1048576.0,
                      double(i.freeram) * i.mem_unit / 1048576.0);
        s << mem;
    }
    s << "jplacer Version: " << version;
    return s.str();
}

void JPlacerDiagnosticsDialog::send() {
    std::vector<std::pair<std::string, std::string>> files;
    if (m_cell->isChecked() && !m_sources.cellFile.empty()) files.push_back({ fs::path(m_sources.cellFile).filename().string(), m_sources.cellFile });
    if (m_parts->isChecked()) files.push_back({ "parts.xml", m_sources.partsFile });
    if (m_packages->isChecked()) files.push_back({ "packages.xml", m_sources.packagesFile });
    if (m_log->isChecked()) files.push_back({ "Latest Log File", m_sources.logFile });
    if (m_job->isChecked() && m_sources.saveJob)
        if (const std::string job = m_sources.saveJob(); !job.empty()) files.push_back({ fs::path(job).filename().string(), job });
    const std::string text = compose(m_description->text(), m_system->isChecked() ? systemInfo(m_sources.version) : std::string(), files);
    // Named by when it was made.
    const std::time_t now = std::time(nullptr);
    std::tm tm {};
    localtime_r(&now, &tm);
    char name[64];
    std::strftime(name, sizeof name, "jplacer-diagnostics-%Y%m%d-%H%M%S.txt", &tm);
    const fs::path dir = fs::path(JPlacerPaths::configDir()) / "diagnostics";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / name;
    std::ofstream(file, std::ios::binary) << text;
    const bool ok = fs::exists(file, ec) && fs::file_size(file, ec) == text.size();
    close();
    if (m_written) m_written(ok ? file.string() : std::string(), ok ? std::string() : "it could not be written in " + dir.string());
}

void JPlacerDiagnosticsDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = 2 * st.fieldPadding, x = pad, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    const float noteW = cw - 2 * st.fieldPadding;
    for (JLabel* n : m_notes) n->setMinimumSize(0.f, std::max(st.labelHeight, n->heightFor(noteW)));
    m_page->setBounds({ x, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_page->getNodeId(), DirtySelf);
    const JRect b = m_page->bounds();
    graph().computeLayout(m_page->getNodeId(), { b.width, b.width, b.height, b.height });
    m_buttons->setBounds({ x, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
