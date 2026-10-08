// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardLocation.h"
#include "JPPanel.h"
#include "JPPanelLocation.h"
#include "JPPlacement.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

inline namespace jf {

// A job, as OpenPnP's Job (a .job.xml file): a root panel holding the
// boards and panels to be placed, each where it lies on the machine, and
// what the job sets on them by unique id ("Pnl1⇒Brd2⇒R1"): which
// placements are placed, enabled states, fiducial checks and error
// handling that differ from their boards' and panels'.
class JPJob {
public:
    enum class ErrorHandling { Alert, Defer };
    static constexpr const char* kExtension = ".job.xml";

    JPJob();
    JPJob(const JPJob&) = delete;
    JPJob& operator=(const JPJob&) = delete;

    JPPanelLocation&       root() { return *m_root; }
    const JPPanelLocation& root() const { return *m_root; }

    std::vector<JPBoardLocation*>             boardLocations() const;
    // The root first, then every panel under it.
    std::vector<JPPanelLocation*>             panelLocations() const;
    std::vector<JPPlacementsHolderLocation*>  boardAndPanelLocations() const;
    JPPlacementsHolderLocation* addBoardOrPanelLocation(std::unique_ptr<JPPlacementsHolderLocation> l);
    void removeBoardOrPanelLocation(const JPPlacementsHolderLocation* l);
    int  instanceCount(const JPPlacementsHolder& h) const;

    // Placements to be placed under `l` (enabled, on its side up, not
    // fiducials), and those not yet placed.
    int totalActivePlacements(const JPPlacementsHolderLocation* l) const;
    int activePlacements(const JPPlacementsHolderLocation* l) const;

    void storePlacedStatus(const JPPlacementsHolderLocation& l, const std::string& placementId, bool placed);
    bool retrievePlacedStatus(const JPPlacementsHolderLocation& l, const std::string& placementId) const;
    void removePlacedStatus(const JPPlacementsHolderLocation& l, const std::string& placementId);
    void removeAllPlacedStatus() { placedStatusMap.clear(); }
    // `placement` null: the board or panel itself.
    void storeEnabledState(const JPPlacementsHolderLocation& l, const JPPlacement* placement, bool enabled);
    bool retrieveEnabledState(const JPPlacementsHolderLocation& l, const JPPlacement* placement) const;
    void removeAllEnabledState() { enabledStateMap.clear(); }
    void storeCheckFiducialsState(const JPPlacementsHolderLocation& l, bool check);
    bool retrieveCheckFiducialsState(const JPPlacementsHolderLocation& l) const;
    void removeAllCheckFiducialsState() { checkFiducialsStateMap.clear(); }
    void storeErrorHandlingState(const JPPlacementsHolderLocation& l, const JPPlacement& p, JPPlacement::ErrorHandling e);
    JPPlacement::ErrorHandling retrieveErrorHandlingState(const JPPlacementsHolderLocation& l, const JPPlacement& p) const;
    void removeAllErrorHandlingState() { errorHandlingStateMap.clear(); }
    // A placement's error handling, Default taking the job's.
    JPPlacement::ErrorHandling effectiveErrorHandling(const JPPlacement& p) const;

    std::map<std::string, bool>                       placedStatusMap;
    std::map<std::string, bool>                       enabledStateMap;
    std::map<std::string, bool>                       checkFiducialsStateMap;
    std::map<std::string, JPPlacement::ErrorHandling> errorHandlingStateMap;
    ErrorHandling                                     errorHandling = ErrorHandling::Alert;
    // jplacer's plan (DESIGN.md, Job execution; JPJobPlan): how the job's part groups are ordered, by a sort
    // rule (JPJobPlan::kSorts) and the parts moved by hand (in their order; the rest after them, sorted).
    std::string                                       planSort;
    std::vector<std::string>                          planOrder;
    std::optional<double>                             version;   // none: an older file
    std::string                                       file;
    bool                                              dirty = false;

    // An older job's boards and panels (version 1), converted by JPConfiguration::loadJob.
    std::vector<std::unique_ptr<JPBoardLocation>> legacyBoardLocations;
    std::vector<JPPanel>                          legacyPanels;

    // Read: the root panel's children are not resolved yet (JPConfiguration).
    static std::unique_ptr<JPJob> fromXml(const JPXmlElement& root);
    JPXmlNode toXml() const;

private:
    std::shared_ptr<JPPanel>         m_rootPanel;
    std::unique_ptr<JPPanelLocation> m_root;
};

} // inline namespace jf
