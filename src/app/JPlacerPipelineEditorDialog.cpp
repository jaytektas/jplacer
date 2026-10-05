// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerPipelineEditorDialog.h"

#include <j/core/Dialog.h>
#include <j/core/JStyle.h>
#include <j/core/MainThreadDispatcher.h>

#include <algorithm>

inline namespace jf {

namespace {

// A twentieth of the main window kept clear on each side, as OpenPnP's.
constexpr int kMarginShare = 20;

} // namespace

JPlacerPipelineEditorDialog::JPlacerPipelineEditorDialog(const std::string& title, std::shared_ptr<JPPipeline> pipeline,
                                                         const std::string& original, Owner owner, ChooseClass chooseClass,
                                                         std::function<void(bool)> closed, JGpuHal& hal, int sx, int sy,
                                                         NativeWinHandleType parent)
    : JDialogWindow(title, kW, kH, hal, sx, sy, parent), m_pipeline(std::move(pipeline)), m_closed(std::move(closed)) {
    setResizable(true, kW / 2, kH / 2);
    if (owner.width > 0 && owner.height > 0) {
        const int mx = owner.width / kMarginShare, my = owner.height / kMarginShare;
        window().setSize(uint32_t(std::max(int(kW / 2), owner.width - 2 * mx)), uint32_t(std::max(int(kH / 2), owner.height - 2 * my)));
        window().setPosition(owner.x + mx, owner.y + my);
    }
    m_editor = std::make_unique<JPPipelineEditor>(graph(), &hal, *m_pipeline, original);
    m_editor->showError = [](const std::string& t, const std::string& why) { JDialog::message(t, why); };
    m_editor->chooseClass = std::move(chooseClass);
    add(m_editor.get());
}

JPlacerPipelineEditorDialog::~JPlacerPipelineEditorDialog() {
    // Said after this window is gone (a question may follow).
    if (!m_closed) return;
    const bool dirty = m_editor->isDirty();
    JMainThreadDispatcher::instance().post([closed = std::move(m_closed), dirty] { closed(dirty); });
}

void JPlacerPipelineEditorDialog::layout(float w, float h) {
    const float pad = JStyle::current().spacing;
    const float top = contentTop();
    m_editor->setBounds({ pad, top, std::max(0.f, w - 2 * pad), std::max(0.f, h - top - pad) });
    graph().invalidateNode(m_editor->getNodeId(), DirtySelf);
    const JRect b = m_editor->bounds();
    graph().computeLayout(m_editor->getNodeId(), { b.width, b.width, b.height, b.height });
}

} // inline namespace jf
