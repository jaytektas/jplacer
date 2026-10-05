// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPConfiguration.h"
#include "JPLocation.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// OpenPnP's BlindsFeeder, its model: several tapes side by side in a 3D
// printed holder, under a cover with a blind (window) over each pocket that
// the nozzle pushes along to open and close them, or a cover pushed ahead of
// the part picked; the holder's frame on the machine from its three diamond
// fiducials (fiducial 1 its origin, fiducial 2 along the tapes, fiducial 3
// across them): X along the tapes, Y across, so that each tape is at its
// pocket centerline. The feeders on the same holder (fiducial 1 within 2 mm)
// in the same group share the holder's settings: what one is set to, the
// others are set to (propagate).
class JPBlindsFeeders {
public:
    static constexpr const char* kDefaultGroup = "Default";
    // The holder's frame: machine = [a c; b d] * feeder + (tx, ty), turned by `rotation` (degrees).
    struct Frame {
        double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0, rotation = 0;
        bool   fromFiducials = false;   // false: not enough fiducials, fiducial 1's place only
    };
    static Frame      frame(const JPFeeder& feeder);
    static JPLocation feederToMachine(const JPFeeder& feeder, const JPLocation& feederLocation);
    static JPLocation machineToFeeder(const JPFeeder& feeder, const JPLocation& machineLocation);
    // A pixel's angle (OpenCV's, left handed) in the holder's frame.
    static double pixelToFeederAngle(const JPFeeder& feeder, double pixelAngle);

    // The tape length (fiducial 1 to 2, to whole 2 mm) and the holder's extent
    // (fiducial 3 from the nearer of 1 and 2) from the fiducials, kept; the geometry recalculated.
    static void updateFrame(JPConfiguration& config, const std::string& feederId);
    // EIA-481's: pockets half the sprocket pitch apart (2 mm).
    static bool   isSmallPitch(const JPFeeder& feeder);
    // From the tape's start to the first pocket (mm; NaN: no pocket pitch yet).
    static double pocketDistanceMm(const JPFeeder& feeder);
    // The pocket count from the tape length and pitch, the first and last pocket kept within it.
    static void   recalculateGeometry(JPFeeder& feeder);
    // The part's angle in the tape: the holder's tapes run the other way round EIA-481's.
    static double pickRotationInTape(const JPFeeder& feeder);
    // Where pocket `pocket` (1-based; fractions between) is picked, and the pocket fed now.
    static JPLocation pickLocation(const JPFeeder& feeder, double pocket);
    static int        fedPocket(const JPFeeder& feeder);
    // Whether its cover is (known to be) open, or closed.
    static bool coverState(const JPFeeder& feeder, bool open);
    // Within its holder (or only on its fiducial 1).
    static bool isLocationInFeeder(const JPFeeder& feeder, const JPLocation& location, bool fiducial1Only);

    // Its group, a location group name read as the default.
    static std::string              groupName(const JPFeeder& feeder);
    static std::vector<std::string> groupNames(const JPConfiguration& config);
    // The blinds feeders at `location` (or fiducial 1) in the feeder's group, by pocket centerline.
    static std::vector<std::string> connected(const JPConfiguration& config, const std::string& feederId, const JPLocation& location,
                                              bool fiducial1Only);
    // OpenPnP's updateFromConnectedFeeder: the holder's shared settings taken from `from`.
    static void updateFrom(JPFeeder& to, const JPFeeder& from);
    // OpenPnP's updateConnectedFeedersFromThis: the feeders connected at `location` given this one's shared settings.
    static void propagate(JPConfiguration& config, const std::string& feederId, const JPLocation& location, bool fiducial1Only);
    static void propagate(JPConfiguration& config, const std::string& feederId);
    // OpenPnP's updateFromConnectedFeeder(location): the last other feeder
    // connected there taken as the template, the tape's centerline from
    // `location`, the lanes numbered again; the template's id (empty: none).
    static std::string adoptFromConnected(JPConfiguration& config, const std::string& feederId, const JPLocation& location,
                                          bool fiducial1Only);
    static void        renumber(JPConfiguration& config, const std::string& feederId);

    // Setters with OpenPnP's consequences: a fiducial (1: a first fix adopts
    // a connected holder's settings, a move of more than 2 mm moves the whole
    // holder; 2: fiducial 3 turned with it), the centerline (lanes renumbered),
    // the group (joined, renamed or left as OpenPnP allows), the cover's
    // position and the calibration (shared).
    static void setFiducial(JPConfiguration& config, const std::string& feederId, int n, const JPLocation& location);
    static void setPocketCenterline(JPConfiguration& config, const std::string& feederId, double mm);
    static void setGroupName(JPConfiguration& config, const std::string& feederId, const std::string& name);
    static void setCoverPosition(JPConfiguration& config, const std::string& feederId, std::optional<double> mm);
    static void setCalibrated(JPConfiguration& config, const std::string& feederId, bool calibrated);

    // The OCR label's corners (upper left, upper right, lower left) in the holder's frame, for a tape at `feederYMm`.
    static std::array<JPLocation, 3> ocrRegionCorners(const JPFeeder& feeder, double feederYMm);
    // The blinds-covered feeders with one of `actuations` whose cover is not yet open (or closed).
    static std::vector<std::string> coversToActuate(const JPConfiguration& config, const std::vector<std::string>& actuations, bool open);
    // OpenPnP's getJobPreparationLocation: an uncalibrated (with vision), OCR checking or to be opened feeder's first pocket's place.
    static std::optional<JPLocation> jobPreparationLocation(const JPFeeder& feeder);
};

} // inline namespace jf
