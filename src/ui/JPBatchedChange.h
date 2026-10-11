// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <functional>

inline namespace jf {

// A table model's change said once for many edits (JPTableModel::beginEdits, endEdits): a change made
// while edits are begun is said as they end, once; one made alone is said at once.
class JPBatchedChange {
public:
    void begin() { ++m_depth; }
    void end(const std::function<void()>& say) {
        if (m_depth == 0 || --m_depth > 0 || !m_pending) return;
        m_pending = false;
        if (say) say();
    }
    void changed(const std::function<void()>& say) {
        if (m_depth > 0) m_pending = true;
        else if (say) say();
    }

private:
    int  m_depth = 0;
    bool m_pending = false;
};

} // inline namespace jf
