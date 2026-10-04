// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPSetupProperties.h"

#include "model/JPConfiguration.h"

#include <functional>
#include <string>

inline namespace jf {

// A vision settings' page on the Vision tab, as OpenPnP's
// BottomVisionSettingsConfigurationWizard and
// FiducialVisionSettingsConfigurationWizard lay it out (shown by
// JPSetupForm): General (Name, Assigned to, Manage Settings, Enabled?, and
// for bottom vision Pre-rotate, Rotation, Part size check, Size tolerance),
// then Test Alignment and Vision Offsets, or Fiducial Locator. Edits are
// made at once.
class JPVisionForms {
public:
    // `usedIn`: what uses it, as Assigned To lists it.
    static JPSetupProperties::Form forSettings(JPConfiguration& config, const std::string& id, const std::string& usedIn);
    // A button of the form on the settings; true when it changed them.
    static bool act(JPConfiguration& config, const std::string& id, const std::string& action);
};

} // inline namespace jf
