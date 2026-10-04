// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerImportDialog.h"

#include "ui/JPFieldGrid.h"
#include "ui/JPUiParts.h"

#include <j/core/Dialog.h>
#include <j/core/JButton.h>
#include <j/core/JStyle.h>

#include <algorithm>

inline namespace jf {

JPlacerImportDialog::JPlacerImportDialog(const JPBoardImporter& importer, JPConfiguration& config,
                                         std::function<void(JPBoard&)> onImported, JGpuHal& hal, int sx, int sy,
                                         NativeWinHandleType parent)
    : JDialogWindow(importer.description(), kW, kH, hal, sx, sy, parent)
    , m_importer(importer)
    , m_config(config)
    , m_onImported(std::move(onImported)) {
    setResizable(true, kW / 2, kH / 2);
    JSceneGraph& g = graph();
    const JStyle& st = JStyle::current();
    m_content = std::make_unique<JContainer>(g, 0.f, 0.f);
    m_content->setDirection(JFlexDirection::Column)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);

    // Files: each label against its path, then Browse.
    auto filesGrid = std::make_unique<JPFieldGrid>(g, 1);
    for (const auto& f : importer.files()) {
        auto row = JPUiParts::row(g);
        JLineEdit* path = row->add(std::make_unique<JLineEdit>(g, ""));
        path->setHSizePolicy(JSizePolicyMode::Expanding, 1);
        m_files.push_back(path);
        JButton* browse = row->add(JPUiParts::button(g, "Browse"));
        browse->setTooltip("Browse");
        browse->onClicked.connect([path, title = importer.description(), extensions = f.extensions] {
            JDialog::openFile(title, extensions, [path](std::string chosen) { path->setText(chosen); });
        });
        filesGrid->widget(f.label, std::move(row));
    }
    m_content->add(JPFieldGrid::grouped(g, "Files", std::move(filesGrid)));

    // Options: a tick box each, as they start.
    auto optionsGrid = std::make_unique<JPFieldGrid>(g, 1);
    for (const auto& o : importer.options()) {
        auto box = std::make_unique<JCheckBox>(g, o.label, 0.f);
        box->setChecked(o.initial);
        if (!o.tooltip.empty()) box->setTooltip(o.tooltip);
        m_options.push_back(optionsGrid->widget("", std::move(box)));
    }
    m_content->add(JPFieldGrid::grouped(g, "Options", std::move(optionsGrid)));
    add(m_content.get());

    m_buttons = std::make_unique<JDialogButtonBox>(g);
    m_buttons->addButton("Cancel", JDialogButtonBox::Role::Reject)->setTooltip("Cancel");
    m_buttons->addButton("Import", JDialogButtonBox::Role::Accept)->setTooltip("Import");
    m_buttons->onReject.connect([this] { close(); });
    m_buttons->onAccept.connect([this] { runImport(); });
    add(m_buttons.get());
}

void JPlacerImportDialog::runImport() {
    std::vector<std::string> files;
    for (const JLineEdit* e : m_files) files.push_back(e->text());
    std::vector<bool> options;
    for (const JCheckBox* b : m_options) options.push_back(b->isChecked());
    JPBoard board;
    std::string error;
    if (!m_importer.read(files, options, m_config, board, error)) {
        JDialog::message("Import Error", m_importer.failureText(error));
        return;
    }
    close();
    if (m_onImported) m_onImported(board);
}

void JPlacerImportDialog::layout(float w, float h) {
    const JStyle& st = JStyle::current();
    const float pad = st.spacing * 2, cw = w - 2 * pad;
    const float buttonsY = h - pad - st.buttonHeight;
    const float top = contentTop();
    m_content->setBounds({ pad, top, cw, std::max(0.f, buttonsY - st.spacing * 2 - top) });
    graph().invalidateNode(m_content->getNodeId(), DirtySelf);
    const JRect b = m_content->bounds();
    graph().computeLayout(m_content->getNodeId(), { b.width, b.width, 0.f, b.height });
    m_buttons->setBounds({ pad, buttonsY, cw, st.buttonHeight });
}

} // inline namespace jf
