// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementsViewer.h"

#include "JPColourKey.h"
#include "JPUiParts.h"

#include "camera/JPImageFile.h"

#include <j/core/Dialog.h>
#include <j/core/JLabel.h>
#include <j/core/MainThreadDispatcher.h>
#include <j/core/JStyle.h>
#include <j/core/JTextHelper.h>

#include <algorithm>
#include <thread>

inline namespace jf {

JPPlacementsViewer::JPPlacementsViewer(JSceneGraph& graph, JPPlacementsHolderLocation* root, bool isJob)
    : JContainer(graph, 0.f, 0.f), m_isJob(isJob) {
    const JStyle& st = JStyle::current();
    setDirection(JFlexDirection::JRow)->setAlignItems(JAlignItems::Stretch)->setGap(st.spacing);
    m_canvas = add(std::make_unique<JPPlacementsViewerCanvas>(graph));

    auto side = std::make_unique<JContainer>(graph, 0.f, 0.f);
    JPUiParts::asPanel(*side);
    side->setHSizePolicy(JSizePolicyMode::Fixed);
    m_name = side->add(std::make_unique<JLabel>(graph, "", 0.f));
    m_side = side->add(JPUiParts::button(graph, "Viewing From Top"));
    m_side->setTooltip("Toggles between viewing from the top or bottom");
    m_side->setEnabled(!isJob);
    m_side->onClicked.connect([this] {
        m_canvas->viewFromTop = !m_canvas->viewFromTop;
        viewingSideChanged();
    });
    std::vector<std::string> options { "Viewing All Descendants", "Viewing Children Only" };
    if (isJob) options.push_back("Viewing Selected Only");
    m_viewing = side->add(std::make_unique<JComboBox>(graph, options, 0.f));
    m_viewing->setTooltip("Toggles between showing only the direct children, all of the descendants or selected in job grid");
    m_viewing->onIndexChanged.connect([this](int i) {
        m_canvas->viewing = i == 0 ? JPPlacementsViewerCanvas::Viewing::All
                          : i == 1 ? JPPlacementsViewerCanvas::Viewing::Children
                                   : JPPlacementsViewerCanvas::Viewing::Selected;
        m_canvas->regenerate();
    });
    struct Option {
        const char* label;
        const char* tooltip;
        bool JPPlacementsViewerCanvas::*flag;
    };
    for (const Option& o : {
             Option { "Reticle", "Overlays a grid reticle over the image", &JPPlacementsViewerCanvas::showReticle },
             Option { "Board/Panel Locations",
                      "Overlays a plus symbol at the location of each visible Board and/or Panel. The positive Y "
                      "direction is indicated by the cyan colored leg similar to the camera view crosshairs. This "
                      "indicates the location where the camera should move if directed to the location of the Board "
                      "or Panel.",
                      &JPPlacementsViewerCanvas::showLocations },
             Option { "Board/Panel Origins",
                      "Overlays a symbol at the origin of each visible Board and/or Panel's coordinate system. The "
                      "positive X direction is indicated by a red colored arrow and the positive Y direction is "
                      "indicated by a cyan colored arrow.",
                      &JPPlacementsViewerCanvas::showOrigins },
             Option { "Fiducials",
                      "Overlays a 1mm diameter circle filled with a copper color at each fiducial location. A plus "
                      "symbol indicates the fiducial's center.",
                      &JPPlacementsViewerCanvas::showFiducials },
             Option { "Placements",
                      "Overlays a 1mm square at the location of each visible Placement. The square is white if the "
                      "Placement is enabled and gray if disabled. In addition a small plus symbol marks the center of "
                      "the placement with the positive Y direction indicated by the cyan colored leg similar to the "
                      "camera view crosshairs. This indicates the location where the camera should move if directed "
                      "to the location of the Placement.",
                      &JPPlacementsViewerCanvas::showPlacements } }) {
        JCheckBox* box = side->add(std::make_unique<JCheckBox>(graph, o.label, 0.f));
        box->setTooltip(o.tooltip);
        m_options.push_back({ box, o.flag });
        box->onStateChanged.connect([this, flag = o.flag](bool on) {
            m_canvas->*flag = on;
            m_canvas->regenerate();
        });
    }
    // As wide as its options, the hints wrapping to fit, the drawing taking the rest.
    float widest = 0;
    for (const char* t : { "Viewing All Descendants", "Viewing From Bottom", "Board/Panel Locations" })
        widest = std::max(widest, JTextHelper::measureWidth(t));
    const float inner = widest + st.controlHeight + 2 * st.spacing;
    m_inner = inner;
    // A board's photos: the list, Delete Photo…, and what was said of the one shown.
    m_photoLabel = side->add(std::make_unique<JLabel>(graph, "Photo", 0.f));
    m_photos = side->add(std::make_unique<JComboBox>(graph, std::vector<std::string> { "None" }, 0.f));
    m_photos->setTooltip("A photo of the board (taken from the Job tab, or as a job starts or ends) shown under the "
                         "drawing, from the side that was up");
    m_photos->onIndexChanged.connect([this](int i) { showPhoto(i); });
    m_deletePhoto = side->add(JPUiParts::button(graph, "Delete Photo\xE2\x80\xA6"));
    m_deletePhoto->setTooltip("Delete the photo shown, from beside the board's file");
    m_deletePhoto->onClicked.connect([this] { deletePhoto(); });
    m_photoNotes = side->add(std::make_unique<JLabel>(graph, "", 0.f));
    m_photoNotes->setWordWrap(true);
    side->add(std::make_unique<JLabel>(graph, "Hints:", 0.f));
    side->add(std::make_unique<JPColourKey>(graph, Colors::Success, "Top outlines"));
    side->add(std::make_unique<JPColourKey>(graph, Colors::Accent, "Bottom outlines"));
    for (const char* hint : { "Use mouse scroll wheel to zoom", "Left click and drag mouse to pan",
                              "Right click on items to open a popup menu" }) {
        JLabel* l = side->add(std::make_unique<JLabel>(graph, hint, 0.f));
        l->setWordWrap(true);
        l->setMinimumSize(0.f, std::max(st.labelHeight, l->heightFor(inner)));
    }
    side->setSize(inner + 2 * st.fieldPadding, 0.f);
    // Its height is the drawing's: where there is less room its last lines are cut off.
    side->setVSizePolicy(JSizePolicyMode::Expanding, 1);
    side->setMinimumSize(inner + 2 * st.fieldPadding, 0.f);
    add(std::move(side));
    setRoot(root);
}

void JPPlacementsViewer::setRoot(JPPlacementsHolderLocation* root) {
    const bool panel = root && root->kind() == JPPlacementsHolderLocation::Kind::Panel;
    // What is shown at first: a board's everything but the reticle, a panel's locations.
    if (root && (!m_shown || m_shownKind != root->kind())) {
        const bool board = !panel;
        m_canvas->showPlacements = m_canvas->showFiducials = m_canvas->showOrigins = board;
        m_canvas->showLocations = true;
        m_shown = true;
        m_shownKind = root->kind();
        for (const auto& [box, flag] : m_options) box->setChecked(m_canvas->*flag);
    }
    m_viewing->setVisible(panel);
    if (!m_isJob) {
        m_canvas->viewing = JPPlacementsViewerCanvas::Viewing::Children;
        m_viewing->setCurrentIndex(1);
    }
    m_canvas->setRoot(root, m_isJob);
    // A board's photos (a board's viewer only: a job's or panel's shows none).
    const bool board = root && !m_isJob && !panel && root->holder;
    const std::string file = board ? root->holder->definition()->file : std::string();
    for (JWidget* w : std::initializer_list<JWidget*> { m_photoLabel, m_photos, m_deletePhoto, m_photoNotes }) w->setVisible(board);
    if (file != m_boardFile) {
        m_boardFile = file;
        m_shownPhoto.clear();
        m_canvas->clearPhoto();
    }
    photosChanged();
}

void JPPlacementsViewer::photosChanged() {
    m_photoList = m_boardFile.empty() ? std::vector<JPBoardPhoto>() : JPBoardPhoto::of(m_boardFile);
    std::vector<std::string> items { m_photoList.empty() ? "None taken" : "None" };
    int current = 0;
    for (size_t i = 0; i < m_photoList.size(); ++i) {
        items.push_back(m_photoList[i].label());
        if (m_photoList[i].file == m_shownPhoto) current = int(i) + 1;
    }
    m_photos->setItems(items);
    m_photos->setEnabled(!m_photoList.empty());
    if (current == 0 && !m_shownPhoto.empty()) {
        m_shownPhoto.clear();
        m_canvas->clearPhoto();
    }
    m_photos->setCurrentIndex(current);
    m_deletePhoto->setEnabled(current > 0);
    if (current == 0) m_photoNotes->setText("");
}

void JPPlacementsViewer::showPhoto(int index) {
    m_deletePhoto->setEnabled(index > 0);
    if (index <= 0 || size_t(index) > m_photoList.size()) {
        m_shownPhoto.clear();
        m_canvas->clearPhoto();
        m_photoNotes->setText("");
        return;
    }
    const JPBoardPhoto photo = m_photoList[size_t(index - 1)];
    if (photo.file == m_shownPhoto) return;
    m_shownPhoto = photo.file;
    std::string notes;
    for (const std::string& n : photo.notes) notes += (notes.empty() ? "" : "\n") + n;
    m_photoNotes->setText(notes);
    m_photoNotes->setMinimumSize(0.f, notes.empty() ? 0.f : m_photoNotes->heightFor(m_inner));
    m_photos->setTooltip(photo.label());
    // Read off the screen's thread (a photo is large), shown once it is.
    std::weak_ptr<bool> alive = m_alive;
    std::thread([this, alive, photo] {
        auto picture = std::make_shared<JPFrame>();
        std::string why;
        const bool ok = JPImageFile::readPng(photo.file, *picture, why);
        JMainThreadDispatcher::instance().post([this, alive, photo, picture, ok, why] {
            if (const auto a = alive.lock(); !a || m_shownPhoto != photo.file) return;
            if (!ok) {
                m_photoNotes->setText(why);
                m_photoNotes->setMinimumSize(0.f, m_photoNotes->heightFor(m_inner));
                return;
            }
            m_canvas->setPhoto(picture, photo);
            // From the side that was up.
            if (!m_canvas->photoSeen() && !m_isJob) {
                m_canvas->viewFromTop = !m_canvas->viewFromTop;
                viewingSideChanged();
            }
        });
    }).detach();
}

void JPPlacementsViewer::deletePhoto() {
    const int index = m_photos->currentIndex();
    if (index <= 0 || size_t(index) > m_photoList.size()) return;
    const JPBoardPhoto photo = m_photoList[size_t(index - 1)];
    std::weak_ptr<bool> alive = m_alive;
    JDialogOptions opts;
    opts.okLabel = "Delete";
    JDialog::confirm("Delete Photo?", "Delete the photo " + photo.label() + "? It cannot be brought back.",
                     [this, alive, photo] {
                         if (const auto a = alive.lock(); !a) return;
                         std::string why;
                         if (!photo.remove(why)) {
                             JDialog::message("Photo Not Deleted", why);
                             return;
                         }
                         photosChanged();
                     },
                     {}, opts);
}

void JPPlacementsViewer::setShown(bool JPPlacementsViewerCanvas::*flag, bool on) {
    m_canvas->*flag = on;
    for (const auto& [box, f] : m_options)
        if (f == flag) box->setChecked(on);
    m_canvas->regenerate();
}

void JPPlacementsViewer::viewingSideChanged() {
    m_side->setLabel(m_canvas->viewFromTop ? "Viewing From Top" : "Viewing From Bottom");
    m_canvas->regenerate();
}

} // inline namespace jf
