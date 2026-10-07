// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPlot.h"
#include "JPVisionTests.h"
#include "machine/JPMotionTestResult.h"

#include "camera/JPFrame.h"
#include "machine/JPCellConfig.h"
#include "model/JPConfiguration.h"
#include "model/JPLocation.h"
#include "machine/JPFirmwareProfile.h"

#include <j/core/JPropertyModel.h>

#include <functional>
#include <memory>
#include <optional>
#include <map>
#include <string>
#include <vector>

inline namespace jf {

// What Machine Setup edits on each part of a cell: its settings as a
// property model, read from and written to the cell being set up, and how
// they are laid out, as OpenPnP lays out the same part: tabs, titled groups
// in each, and rows in each group (a label, then one or more settings side
// by side; X / Y / Z / Rotation as columns under a header; a place, with
// buttons to take it from where the machine is or go there). Only settings
// jplacer acts on are here; whatever else a cell file carries (brought from
// OpenPnP, for features not built yet) is kept as it is.
//
// A choice (a controller, an axis, a head) is shown and set by name.
class JPSetupProperties {
public:
    // A setting in a row: the property, and what is said before it ("" for
    // the first in a row, which the row's label names).
    struct Cell {
        std::string property;   // empty: an empty place, keeping the columns
        std::string label;
        // A button (labelled `label`) whose action is `property`, the owner's to do.
        bool        button = false;
        bool        enabled = true;
        std::string tooltip;
        // A button shown as this icon (OpenPnP's icon's name), its label then its name.
        std::string icon;
        // A button of its own row across the group (OpenPnP's Auto Setup).
        bool        wide = false;
    };
    // What a place row's buttons use: the camera on the head, or the tool
    // chosen (a nozzle); and what an axis row takes, an axis's position.
    enum class Place { None, Location, Axis };
    struct Row {
        enum class Kind {
            Fields,    // label, settings
            Header,    // the column titles over the rows after it (cells' labels)
            Note,      // a line of text
            Actions,   // buttons (cells' labels; property: the action's name)
            Plot,      // a graph (plot), titled by its label
            Image,     // a picture (image), by its label
            Strip,     // a search's progress (strip), shown while it has cells
        };
        Kind        kind = Kind::Fields;
        std::string label;
        std::vector<Cell> cells;
        std::string text;                // a note
        std::string tooltip;             // what its label says when pointed at
        Place       place = Place::None; // its cells are X, Y, Z, rotation (Location) or one axis (Axis)
        std::string axis;                // Place::Axis: which axis
        // Place::Axis: what its capture and move buttons say (empty: the general words).
        std::string captureTip, moveTip;
        std::shared_ptr<const JPPlot> plot;   // Kind::Plot
        // Kind::Plot: the graph now, read again on a refresh (when set, in place of `plot`).
        std::function<std::shared_ptr<const JPPlot>()> plotNow;
        // Kind::Strip: each cell's state (JPSearchStrip's), read again on a refresh.
        std::function<std::vector<int>()> strip;
        // Kind::Image: the picture now (null: none), read again on a refresh;
        // shown in a square box, or (an illustration) at its own size.
        std::function<std::shared_ptr<const JPFrame>()> image;
        bool ownSize = false;
        // Kind::Image: drawn without its box (on the form, as OpenPnP's HsvIndicator).
        bool unframed = false;
        // Kind::Image: words beside the picture, read again on a refresh;
        // paragraphs (split by an empty line) with a line between them.
        std::function<std::string()> beside;
        // Place::Location: the actuator its tool buttons use (OpenPnP's
        // LocationButtonsPanel actuatorName), read when it is shown; none
        // or empty: the nozzle chosen.
        std::function<std::string()> actuator;
        // OpenPnP's optional location buttons: Position Tool (Without Safe Z), and
        // Contact Probe Tool (the place's Z found by a contact probing nozzle).
        bool positionNoSafeZ = false;
        bool contactProbe = false;
        // Place::Location: what its X, Y, Z and rotation are offsets from
        // (OpenPnP's LocationButtonsPanel baseLocation): taken from where the
        // tool is less it, turned back by its rotation; gone to as it, plus
        // them turned by its rotation. None: they are the machine's own.
        std::function<std::optional<JPLocation>()> base;
    };
    struct Group {
        std::string      title;
        std::vector<Row> rows;
    };
    struct Tab {
        std::string        title;
        std::vector<Group> groups;
    };

    struct Form {
        std::string    title;       // what is being edited ("Axis x")
        JPropertyModel model;
        std::vector<Tab> tabs;
        // Properties whose change changes which others there are (an axis's
        // kind, a camera's head): the form is made again after one changes.
        std::vector<std::string> reshaping;
        // Properties that only change what is shown (a choice for this
        // session, a picture to look at), not the part: no step to undo.
        std::vector<std::string> viewOnly;
        // Buttons that change the part themselves (a row added to a table, or
        // taken away), by their action: what it is called as a step to undo,
        // and the change. The form is made again after one.
        struct Edit {
            std::string           what;
            std::function<void()> apply;
        };
        std::map<std::string, Edit> edits;
        // Properties (and buttons, by action) shown but not changeable now (OpenPnP's disabled or not
        // editable fields: a setting that does nothing as the others stand,
        // or one a calibration sets): greyed.
        std::vector<std::string> disabled;
    };

    // The form for the node at `path` (JPSetupTree); an empty model for a
    // group, or a part not in `cell`. `profiles`: the firmware profiles a
    // controller can name (and whose commands it can replace). The model refers to `cell`, which must outlive it.
    // `config`: the vision settings the Vision nodes choose from (none: only
    // the one set), and whose default settings' page they show (edited there,
    // as the Vision tab edits them; `tests`: what its tests work with).
    // `motionTest`: the last motion planner Test Motion run, for the
    // machine's Motion Planner Diagnostics (none: not run yet).
    // `live`: what the running machine shows the forms (none: not shown).
    struct Live {
        // A template picture by its file name (a nozzle tip's changer slot vision).
        std::function<std::shared_ptr<const JPFrame>(const std::string& fileName)> templatePicture;
        // A camera's device settings as it has them (JPCaptureSource::controls).
        std::function<JJson(const std::string& cameraId)> cameraControls;
        // The Z calibration offset of the nozzle a tip is loaded on (OpenPnP's calibrationOffsetZ; none: not calibrated).
        std::function<std::optional<double>(const std::string& nozzleTipId)> zCalibration;
        // A controller's G-code console (OpenPnP's driver Console tab): its traffic, newest last.
        std::function<std::vector<std::string>(const std::string& driverId)> driverConsole;
    };
    static Form forNode(JPCellConfig& cell, const std::string& path, const std::vector<JPFirmwareProfile>& profiles,
                        JPConfiguration* config = nullptr, const JPVisionTests* tests = nullptr,
                        const JPMotionTestResult* motionTest = nullptr, const Live& live = {});
    // A tab's words as Machine Setup's search looks at them, lower-cased, a line each: its title, its
    // groups', and its rows' and settings' names.
    static std::string words(const Tab& tab);
    // The New ID chosen on a NeoDen 4 feeder actuator's form, for its Change
    // Feeder ID (not kept).
    static int& neoden4NewFeederId();
    // A driver Console's command line as typed (sent by "consoleSend") and its Force Upper Case (not kept).
    static std::string& consoleCommand();
    static bool& consoleUpperCase();
    static constexpr size_t kConsoleLines = 24;   // the console's lines shown
};

} // inline namespace jf
