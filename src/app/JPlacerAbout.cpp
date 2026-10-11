// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerAbout.h"

#include "camera/JPImageFile.h"
#include "common/JPlacerLog.h"
#include "ui/JPOpenPnpIcons.h"

#include <j/core/Dialog.h>
#include <j/core/Log.h>

#include <string>
#include <utility>

inline namespace jf {

namespace {

// The logo, sized for the box (icons/jaytek-logo.png, made from its 1024-pixel artwork for the size it is
// drawn at, as jayecu Studio's is).
constexpr const char* kLogo = "jaytek-logo.png";

// The logo's pixels into the request, as RGBA rows; none when it cannot be read (the box shows its words).
void attachLogo(JDialogRequest& req) {
    const JPOpenPnpIcons* icons = JPOpenPnpIcons::instance();
    const std::string path = icons ? icons->picture(kLogo) : std::string();
    if (path.empty()) {
        JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << "About: the logo " << kLogo << " is not beside the icons";
        return;
    }
    JPFrame logo;
    std::string why;
    if (!JPImageFile::readPng(path, logo, why) || logo.width <= 0 || logo.height <= 0) {
        JLOGC(JPlacerLog::kUi, JLogLevel::Warn) << "About: the logo could not be read: " << why;
        return;
    }
    req.imageRgba = std::move(logo.rgba);
    req.imageWidth = uint32_t(logo.width);
    req.imageHeight = uint32_t(logo.height);
}

} // namespace

void JPlacerAbout::show() {
    JDialogRequest req;
    req.kind = JDialogRequest::JKind::Message;
    req.title = "About jplacer";
    attachLogo(req);
    req.body = std::string("jplacer\n"
                           "Pick-and-place machine control.\n"
                           "\n"
                           "Version ") + JPLACER_VERSION + "\n"
               "\n"
               "Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>\n"
               "This program is free software: you can redistribute it and/or modify it under the terms of the GNU "
               "General Public License, version 3 or later. It comes with ABSOLUTELY NO WARRANTY.\n"
               "\n"
               "https://github.com/jaytektas/jplacer\n"
               "\n"
               "Built on JFramework.";
    JDialogManager::instance().push(std::move(req));
}

} // inline namespace jf
