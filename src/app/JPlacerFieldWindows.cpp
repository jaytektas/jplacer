// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerFieldWindows.h"

#include "JPlacerFieldDialog.h"
#include "JPlacerFieldsDialog.h"

#include "ui/JPTable.h"

inline namespace jf {

JPlacerFieldWindows::JPlacerFieldWindows(JAppWindow& window, JPlacerJob& job, std::function<void()> changed)
    : m_window(window), m_job(job), m_changed(std::move(changed)) {
    JPTable::s_fields = &m_job.configuration().fields();
    JPTable::s_fieldHooks.add = [this](JPFieldSet::Of of, std::function<void(const std::string&)> added) { add(of, std::move(added)); };
    JPTable::s_fieldHooks.edit = [this](JPFieldSet::Of of, const std::string& key) { edit(of, key); };
    JPTable::s_fieldHooks.manage = [this](JPFieldSet::Of of) { manage(of); };
}

JPlacerFieldWindows::~JPlacerFieldWindows() {
    JPTable::s_fieldHooks = {};
    JPTable::s_fields = nullptr;
}

void JPlacerFieldWindows::add(JPFieldSet::Of of, std::function<void(const std::string&)> added) {
    m_window.openModal<JPlacerFieldDialog>(m_job.configuration(), &m_job.job(), of, std::string(),
                                           [this, added = std::move(added)](const std::string& key, bool) {
                                               kept();
                                               if (added) added(key);
                                           });
}

void JPlacerFieldWindows::edit(JPFieldSet::Of of, const std::string& key) {
    m_window.openModal<JPlacerFieldDialog>(m_job.configuration(), &m_job.job(), of, key,
                                           [this](const std::string&, bool) { kept(); });
}

void JPlacerFieldWindows::manage(JPFieldSet::Of of) {
    m_window.openModal<JPlacerFieldsDialog>(
        m_job.configuration(), &m_job.job(), of,
        [this, of](const std::string& key) {
            if (key.empty()) add(of, nullptr);
            else edit(of, key);
        },
        [this] { kept(); });
}

void JPlacerFieldWindows::kept() {
    m_job.configurationChanged();   // fields.json and the library (values taken out, read anew) saved
    if (m_job.job().dirty) m_job.changed();
    JPTable::fieldsChanged();
    if (m_changed) m_changed();
}

} // inline namespace jf
