// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPReticle.h"

#include "camera/JPCameraFeed.h"
#include "camera/JPStraightener.h"

#include <j/core/JWidget.h>
#include <j/core/MenuSystem.h>
#include <j/graphics/GpuHal.h>

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A camera's live picture, fitted to the widget with its shape kept, and a
// crosshair through the centre: the point the camera is looking at.
//
// The mouse wheel zooms in and out about the centre, so the crosshair stays
// on the point the camera is looking at; zoomed, the zoom is shown in a
// corner. Fitted (1x) is as far out as it goes.
//
// Right-click it for the reticle (JPReticle) and to fit the picture again.
// To move the camera to a point in the picture: double-click it, Shift+click
// it, or drag from anywhere to it (a line shows the move until the button is
// let go; let go outside the picture and nothing moves).
//
// Each new frame becomes a GPU texture on the main thread (the feed's signal
// is re-posted there); the previous texture is released.
class JPCameraView : public JWidget {
public:
    JPCameraView(JSceneGraph& graph, JGpuHal& hal);
    ~JPCameraView() override;

    // The feed to show; null shows nothing. The view does not own it.
    void setFeed(JPCameraFeed* feed);
    // What to say in place of a picture (no camera, why it stopped).
    void setMessage(const std::string& text);
    // OpenPnP's showFilteredImage: `picture` (as the camera's pixels) shown
    // in place of the live picture for `ms`, `text` over it.
    void showPicture(const JPFrame& picture, const std::string& text, int ms);
    // Show the picture straightened (JPStraightener, drawn as its mesh), or
    // as taken (null). Clicks stay in the picture-as-taken's pixels.
    void setStraightener(std::shared_ptr<const JPStraightener> straightener) {
        m_straight = std::move(straightener);
        invalidate();
    }

    void populateRenderPrimitives(JPrimitiveBuffer& buf) override;
    void handleMousePress(float x, float y) override;
    void handleMouseMove(float x, float y) override;
    void handleMouseRelease(float x, float y) override;
    bool handleScroll(float mx, float my, float wheel) override;
    void prepareContextMenu(float mx, float my) override;

    // The camera's calibration for its pictures, for the reticles drawn in
    // millimetres (not valid: none).
    void setCalibration(const JPCameraCalibration& calibration);
    void setReticle(const JPReticle& reticle);
    const JPReticle& reticle() const { return m_reticle; }
    // The reticle chosen from the menu.
    std::function<void(const JPReticle&)> onReticleChanged;
    // Something else drawn over the picture in millimetres, as OpenPnP's
    // named reticles (a package's footprint): drawn through `place` while
    // the camera is calibrated. Set again by the same key to replace it,
    // null to take it away.
    using Overlay = std::function<void(JVectorCanvas& vg, const JPReticle::Place& place, float line)>;
    void setOverlay(const std::string& key, Overlay overlay);

    // OpenPnP's light toggle, a sun at the top right while the camera has a
    // light (`has`): bright while it is on (`on`; not known: shown off).
    // Clicked, onToggleLight.
    void setLight(bool has, std::optional<bool> on);
    std::function<void()> onToggleLight;
    // A camera fixed to the machine: OpenPnP's Move Selected Nozzle to Camera,
    // first in its menu while set.
    std::function<void()> onMoveNozzleHere;
    // OpenPnP's Estimate Z Coordinate of Object, first in the menu while
    // `canEstimateZ` says (the camera calibrated at two heights).
    std::function<void()> onEstimateZ;
    std::function<bool()> canEstimateZ;
    // OpenPnP's Show Image Info (the menu's): the picture's size, the zoom,
    // the pictures a second and a histogram of its colours, at the top left.
    bool showImageInfo() const { return m_showInfo; }
    void setShowImageInfo(bool on);
    std::function<void(bool on)> onShowImageInfoChanged;
    // OpenPnP's Zoom Sensitivity: a notch of the wheel zooms by 2 (High), by
    // the square root of 2 (Medium, to begin with) or the fourth root (Low).
    enum class ZoomSensitivity { High, Medium, Low };
    static const char* name(ZoomSensitivity s);
    void setZoomSensitivity(ZoomSensitivity s) { m_sensitivity = s; }
    std::function<void(ZoomSensitivity)> onZoomSensitivityChanged;
    // OpenPnP's Rendering Quality: Low draws each pixel as a block (to begin
    // with); High smooths it; BestScale (Highest Quality) smooths it at a
    // whole-number scale only, and the wheel zooms by 2 at least.
    enum class RenderingQuality { Low, High, BestScale };
    static const char* name(RenderingQuality q);
    void setRenderingQuality(RenderingQuality q);
    std::function<void(RenderingQuality)> onRenderingQualityChanged;
    // The live picture shown at most `fps` times a second (0: every picture),
    // and held while `suspended` says (pictures vision shows still shown).
    void setPreviewFps(double fps) { m_previewFps = fps; }
    std::function<bool()> suspended;
    // A picture vision shows (showPicture), for its owner to bring it forward.
    std::function<void()> onPictureShown;
    // How far zoomed in: 1 is the picture fitted to the view.
    double zoom() const { return m_zoom; }
    // When it was last drawn (on screen then).
    std::chrono::steady_clock::time_point drawnAt() const { return m_drawnAt; }
    static constexpr double kMostZoom = 64.0;

    // A point in the picture asked to be looked at (double-click, Shift+click,
    // drag), at this pixel of the picture as taken.
    std::function<void(double px, double py)> onLookAt;
    // While set, a click is a place chosen (OpenPnP's CameraView action: Auto
    // Setup's "click on the center of the first part"): its pixel of the
    // picture as taken; nothing is looked at meanwhile. With what to do,
    // said over the picture (empty: nothing).
    std::function<void(double px, double py)> onPicked;
    void setPrompt(const std::string& text);

    // A SELECTION (OpenPnP's CameraView selection, for a template image or an
    // area of interest): while on, a rectangle in the picture as taken's
    // pixels, drawn with a handle at each corner; dragged inside it moves,
    // by a corner it is resized, from anywhere else a new one is drawn.
    // Nothing is looked at meanwhile.
    struct Selection {
        int x = 0, y = 0, width = 0, height = 0;
    };
    void setSelectionEnabled(bool on);
    bool selectionEnabled() const { return m_selecting; }
    void setSelection(const Selection& s);
    Selection selection() const { return m_selection; }
    // The latest picture cut to the selection; null when there is none (no
    // picture, or an empty selection).
    std::shared_ptr<JPFrame> captureSelection() const;
    // The size of the picture shown (0 before the first).
    int pictureWidth() const { return m_w; }
    int pictureHeight() const { return m_h; }

private:
    void showLatest();
    // Where a corner of the selection is on screen; false when it is not shown.
    bool selectionCorner(double rawX, double rawY, float& sx, float& sy) const;
    void dragSelection(double px, double py);
    void dropTexture();

    JGpuHal&                           m_hal;
    JPCameraFeed*                      m_feed = nullptr;
    TextureHandle                      m_tex  = kNullTexture;
    int                                m_w = 0, m_h = 0;
    uint64_t                           m_have = 0;
    JPFrame                            m_frame;
    // A picture shown in place of the live one until then, and what it says.
    std::chrono::steady_clock::time_point m_stillUntil {};
    std::string                        m_stillText;
    std::string                        m_prompt;
    std::string                        m_message;
    std::shared_ptr<const JPStraightener>    m_straight;
    // Where a pixel of the picture as taken is shown: straightened when straightening.
    bool shown(double rawX, double rawY, double& x, double& y) const;
    // The pixel of the picture as taken at a point on screen; false off the picture.
    bool pixelAt(float x, float y, double& px, double& py) const;
    void lookAt(float x, float y);
    void buildMenu();
    // The light toggle's middle and size on screen.
    void lightToggle(float& cx, float& cy, float& size) const;
    bool inLightToggle(float x, float y) const;
    void drawLightToggle(JVectorCanvas& vg) const;
    void drawImageInfo(JPrimitiveBuffer& buf, float x, float y) const;
    void choose(const JPReticle& reticle);
    // Where the picture was last drawn (widget coordinates) and at what scale,
    // to turn a click into a pixel of it; and the last press, for a double.
    float                              m_picX = 0, m_picY = 0, m_picScale = 0;
    double                             m_zoom = 1.0;
    std::chrono::steady_clock::time_point m_drawnAt;
    JPCameraCalibration                m_cal;
    double                             m_reachMm = 0;   // how far the picture reaches from its middle
    std::map<std::string, Overlay>     m_overlays;
    JPReticle                          m_reticle;
    std::unique_ptr<JMenu>             m_menu, m_spacingMenu, m_sizeMenu, m_zoomMenu;
    ZoomSensitivity                    m_sensitivity = ZoomSensitivity::Medium;
    std::vector<std::pair<JMenuItem*, ZoomSensitivity>> m_zoomItems;
    std::unique_ptr<JMenu>             m_qualityMenu;
    RenderingQuality                   m_quality = RenderingQuality::Low;
    std::vector<std::pair<JMenuItem*, RenderingQuality>> m_qualityItems;
    // The picture shown in place of the live one (showPicture), kept to draw
    // again when the rendering quality changes.
    JPFrame                            m_still;
    bool                               m_showingStill = false;
    void upload(const JPFrame& picture);
    std::vector<std::pair<JMenuItem*, JPReticle::Kind>> m_kindItems;
    std::vector<std::pair<JMenuItem*, double>> m_spacingItems, m_sizeItems;
    JMenuItem*                         m_spacingItem = nullptr;
    JMenuItem*                         m_sizeItem    = nullptr;
    JMenuItem*                         m_fitItem     = nullptr;
    JMenuItem*                         m_uncalibrated = nullptr;
    JMenuItem*                         m_infoItem = nullptr;
    JMenuItem*                         m_nozzleHereItem = nullptr;
    JMenuItem*                         m_estimateZItem = nullptr;
    bool                               m_showInfo = false;
    JRect                              m_shown {};   // the picture as last drawn, cut to the view
    double                             m_previewFps = 0;
    std::chrono::steady_clock::time_point m_lastShown {};
    bool                               m_hasLight = false, m_lightPressed = false;
    std::optional<bool>                m_lightOn;
    // The time between the last pictures, for the pictures a second.
    std::chrono::steady_clock::time_point m_lastPicture {};
    std::deque<double>                 m_intervals;
    // A drag to look somewhere: where it started, and where it is now.
    bool                               m_pressed = false, m_dragging = false;
    float                              m_dragX = 0, m_dragY = 0;
    // The selection, and what a drag does to it: from where (pixels) and
    // the selection then; the corner held (0..3: top left, top right,
    // bottom right, bottom left), -1 moving it, -2 drawing a new one.
    bool                               m_selecting = false;
    Selection                          m_selection;
    bool                               m_selDragging = false;
    int                                m_selCorner = -1;
    double                             m_selFromX = 0, m_selFromY = 0;
    Selection                          m_selFrom;
    std::chrono::steady_clock::time_point m_lastPress;
    float                              m_lastPressX = 0, m_lastPressY = 0;
    std::function<void()>              m_unwatch;
    std::shared_ptr<bool>              m_alive = std::make_shared<bool>(true);
};

} // inline namespace jf
