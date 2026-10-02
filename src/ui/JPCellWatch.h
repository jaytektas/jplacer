// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/MainThreadDispatcher.h>
#include <j/core/Signal.h>

#include <functional>
#include <memory>
#include <vector>

inline namespace jf {

// A view's subscriptions to a cell's signals, delivered on the main thread
// and only while the view lives.
//
// A cell's signals fire on its own threads; a widget may only be touched on
// the main thread, and may be gone by the time a posted call runs. Every
// panel needs exactly this, so it is written once: hold one, call on(), and
// destroying it disconnects everything and drops whatever is still in flight.
class JPCellWatch {
public:
    JPCellWatch() = default;
    JPCellWatch(const JPCellWatch&)            = delete;
    JPCellWatch& operator=(const JPCellWatch&) = delete;

    ~JPCellWatch() {
        for (const auto& d : m_disconnects) d();
        *m_alive = false;
    }

    template <class... A, class F>
    void on(JSignal<A...>& signal, F fn) {
        std::weak_ptr<bool> alive = m_alive;
        m_disconnects.push_back(signal.connect([alive, fn](A... args) {
            JMainThreadDispatcher::instance().post([alive, fn, args...] {
                if (const auto a = alive.lock(); a && *a) fn(args...);
            });
        }));
    }

private:
    std::vector<std::function<void()>> m_disconnects;
    std::shared_ptr<bool>              m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
