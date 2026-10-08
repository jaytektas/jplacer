// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSetupProperties.h"
#include "JPVisionTests.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// A vision settings' page on the Vision tab, as OpenPnP's
// BottomVisionSettingsConfigurationWizard and
// FiducialVisionSettingsConfigurationWizard lay it out (shown by
// JPSetupForm): General (Name, Assigned to, Manage Settings, Enabled?, and
// for bottom vision Pre-rotate, Rotation, Part size check, Size tolerance),
// then Test Alignment and Vision Offsets, or Fiducial Locator. Edits are
// made at once.
class JPFormBuilder;

class JPVisionForms {
public:
    // What the page is shown for: the Vision tab (none), a part or a package
    // (Specialize and Generalize act for it).
    struct Holder {
        enum class Kind { None, Part, Package } kind = Kind::None;
        std::string id;
    };

    // Which of Manage Settings' buttons a page shows (Reset to Default always): Specialize, while the part or
    // package uses settings it shares; Generalize, on a package while some of its parts have their own (all of
    // them put back), on a part while it has its own (put back on its package's, or the machine's).
    struct Manage {
        bool        specialize = false;
        bool        generalize = false;
        std::string generalizeLabel, generalizeTip;
    };
    static Manage manageFor(const JPConfiguration& config, const JPVisionSettings& v, const Holder& holder);

    // What the test buttons work with (JPVisionTests).
    using Tests = JPVisionTests;
    // `usedIn`: what uses it, as Assigned To lists it.
    static JPSetupProperties::Form forSettings(JPConfiguration& config, const std::string& id, const std::string& usedIn,
                                               const Tests* tests = nullptr);
    // The settings' page added to a form being built (a part's or package's
    // tabs), its buttons' actions prefixed "bottom:" or "fiducial:".
    static void addPage(JPFormBuilder& add, JPConfiguration& config, const std::string& id, const std::string& usedIn,
                        const Holder& holder, const Tests* tests = nullptr);
    // A button of a page: "reset" (to the machine's default settings, `machineDefaultId`; the machine's own to
    // the stock ones), "specialize" (a copy of `id`'s settings
    // for the holder, named after it), "generalize" (a package's parts' own
    // settings taken off). False, and why (empty when nothing was done),
    // when it did nothing.
    static bool act(JPConfiguration& config, const std::string& id, const std::string& action, const Holder& holder,
                    const std::string& machineDefaultId, std::string& why);
    // What Generalize takes away: the package's parts with settings of their own of the kind.
    static std::vector<std::string> specializedIn(const JPConfiguration& config, const Holder& holder, JPVisionSettings::Kind kind);
};

} // inline namespace jf
