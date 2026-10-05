// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPVisionPipelineActions.h"

#include "setup/JPVisionPipelines.h"

#include <j/core/Dialog.h>
#include <j/core/JWidget.h>

inline namespace jf {

bool JPVisionPipelineActions::act(JPConfiguration& config, const std::string& settingsId, const std::string& what, const Hooks& hooks) {
    JPVisionSettings* v = config.visionSettings(settingsId);
    if (!v) return false;
    JDialogOptions yesNo;
    yesNo.okLabel = "Yes";
    yesNo.cancelLabel = "No";
    if (what == "editPipeline") {
        if (hooks.edit) hooks.edit(settingsId);
        return true;
    }
    if (what.rfind("parameter:", 0) == 0) {
        if (hooks.preview) hooks.preview(settingsId, what.substr(std::string("parameter:").size()));
        return true;
    }
    if (what == "copyPipeline") {
        JWidget::clipboardSet(JPVisionPipelines::copy(*v));
        return true;
    }
    if (what == "resetPipeline") {
        JDialog::confirm("Reset Pipeline", "This will replace the Pipeline with the default. Are you sure??",
                         [&config, settingsId, hooks] {
                             JPVisionSettings* s = config.visionSettings(settingsId);
                             if (!s) return;
                             JPVisionPipelines::reset(*s, hooks.machineDefault ? hooks.machineDefault(s->kind) : nullptr);
                             if (hooks.changed) hooks.changed();
                         },
                         nullptr, yesNo);
        return true;
    }
    if (what == "pastePipeline") {
        JDialog::confirm("Paste Pipeline", "This will replace the Pipeline with the one on the clipboard.\n\nAre you sure?",
                         [&config, settingsId, hooks] {
                             JPVisionSettings* s = config.visionSettings(settingsId);
                             std::string why;
                             if (!s) return;
                             if (!JPVisionPipelines::paste(*s, JWidget::clipboardGet(), why)) {
                                 JDialog::message("Paste failed", why);
                                 return;
                             }
                             if (hooks.changed) hooks.changed();
                         },
                         nullptr, yesNo);
        return true;
    }
    return false;
}

} // inline namespace jf
