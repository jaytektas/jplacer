// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/app/JDialogWindow.h>
#include <j/core/JCheckBox.h>
#include <j/core/JContainer.h>
#include <j/core/JDialogButtonBox.h>
#include <j/core/JLabel.h>
#include <j/core/JTextArea.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's Help > Submit Diagnostics: the problem described, and what helps
// the developers (the machine's cell file, the library (its parts and packages), the
// latest log, anonymous system information, the current job, saved first)
// put together in one file. OpenPnP uploads it to Pastebin; jplacer sends
// nothing anywhere: the file is written to the diagnostics folder, which is
// opened, for you to attach where you choose (an issue on jplacer's GitHub).
// Opened through JAppWindow::openModal.
class JPlacerDiagnosticsDialog : public JDialogWindow {
public:
    static constexpr uint32_t kW = 560, kH = 600;
    // The latest log, at most this much of its end.
    static constexpr size_t kMostLogBytes = 1u << 20;

    struct Sources {
        std::string cellFile, logFile;
        std::function<std::string()> saveLibrary;   // the library written as text (JSON); its file, or empty
        std::function<std::string()> saveJob;   // the job saved; its file, or empty
        std::string version;
    };
    // `written`: the file written, or empty with why.
    JPlacerDiagnosticsDialog(Sources sources, std::function<void(const std::string& file, const std::string& why)> written,
                             JGpuHal& hal, int sx, int sy, NativeWinHandleType parent);

    // The diagnostics as one text: the description, then each file included under its name (as
    // OpenPnP's "**** name ****"). `files`: (name, path).
    static std::string compose(const std::string& description, const std::string& systemInfo,
                               const std::vector<std::pair<std::string, std::string>>& files);

protected:
    void layout(float w, float h) override;

private:
    void send();
    static std::string systemInfo(const std::string& version);

    Sources                                                   m_sources;
    std::function<void(const std::string&, const std::string&)> m_written;
    std::unique_ptr<JContainer>                               m_page;
    std::vector<JLabel*>                                      m_notes;
    JTextArea*                                                m_description = nullptr;
    JCheckBox *m_cell = nullptr, *m_library = nullptr, *m_log = nullptr, *m_system = nullptr,
              *m_job = nullptr;
    std::unique_ptr<JDialogButtonBox>                         m_buttons;
};

} // inline namespace jf
