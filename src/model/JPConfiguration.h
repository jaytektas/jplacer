// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoard.h"
#include "JPBoardLocation.h"
#include "JPJob.h"
#include "JPPackage.h"
#include "JPPanel.h"
#include "JPPanelLocation.h"
#include "JPPart.h"
#include "JPVisionSettings.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

inline namespace jf {

// What OpenPnP's Configuration holds besides the machine: the parts and
// packages (parts.xml, packages.xml), and the boards and panels known
// (boards.xml, panels.xml list their files), all in one folder; and loading
// and saving jobs, boards and panels. Parts and packages are found by id
// without regard to case, and keep the order they were added in.
class JPConfiguration {
public:
    static constexpr const char* kPartsFile    = "parts.xml";
    static constexpr const char* kPackagesFile = "packages.xml";
    static constexpr const char* kBoardsFile   = "boards.xml";
    static constexpr const char* kPanelsFile   = "panels.xml";
    static constexpr const char* kVisionFile   = "vision-settings.xml";

    explicit JPConfiguration(std::string directory);

    const std::string& directory() const { return m_directory; }
    // Reads what is there (a missing file is an empty list). Boards and
    // panels that cannot be read are left out and named in `problems`.
    bool load(std::vector<std::string>& problems, std::string& error);
    // Writes the parts, packages, and the boards' and panels' lists.
    bool save(std::string& error) const;

    const std::vector<std::shared_ptr<JPPart>>& parts() const { return m_parts; }
    JPPart* part(const std::string& id) const;
    // Replaces one of the same id, where it was.
    void addPart(std::shared_ptr<JPPart> part);
    void removePart(const std::string& id);

    const std::vector<std::shared_ptr<JPPackage>>& packages() const { return m_packages; }
    JPPackage* package(const std::string& id) const;
    void addPackage(std::shared_ptr<JPPackage> package);
    void removePackage(const std::string& id);

    // Vision settings, as named by parts and packages (read; the Vision tab writes them).
    const std::vector<JPVisionSettings>& visionSettings() const { return m_vision; }
    const JPVisionSettings* visionSettings(const std::string& id) const;
    // Placements on the known boards that use the part (OpenPnP's Placements column).
    int placementCount(const std::string& partId) const;

    const std::vector<std::shared_ptr<JPBoard>>& boards() const { return m_boards; }
    const std::vector<std::shared_ptr<JPPanel>>& panels() const { return m_panels; }
    // The board kept in `path`: one already known, else read (and known
    // from now on); a file that is not there is made, an empty board.
    std::shared_ptr<JPBoard> board(const std::string& path, std::string& error);
    std::shared_ptr<JPPanel> panel(const std::string& path, std::string& error);
    void addBoard(std::shared_ptr<JPBoard> board);
    void addPanel(std::shared_ptr<JPPanel> panel);
    void removeBoard(const JPBoard* board);
    void removePanel(const JPPanel* panel);
    bool saveBoard(JPBoard& board, std::string& error) const;
    bool savePanel(JPPanel& panel, std::string& error) const;

    // A job read, its older form converted (a backup kept beside it), its
    // boards and panels found and what it sets on them restored.
    std::unique_ptr<JPJob> loadJob(const std::string& path, std::string& error);
    bool saveJob(JPJob& job, const std::string& path, std::string& error);
    // A job's board or panel found from its file name (as given, beside its
    // parent's file, beside the job's) and given an instance of it.
    bool resolveBoard(JPJob* job, JPBoardLocation& l, std::string& error);
    bool resolvePanel(JPJob* job, JPPanelLocation& l, std::string& error);
    // Every instance of the definition `def`: its uses in the job and on
    // the known panels, at any depth.
    std::vector<JPPlacementsHolder*> instancesOf(const JPPlacementsHolder& def, const JPJob* job) const;
    // The same, as the places they lie (each a board or panel location).
    std::vector<JPPlacementsHolderLocation*> instanceLocationsOf(const JPPlacementsHolder& def, const JPJob* job) const;
    // The known board or panel (to change) that `h` is, or is an instance of.
    JPPlacementsHolder* definitionOf(const JPPlacementsHolder& h) const;
    // Whether `h` is used in the job, or on any panel but itself.
    bool isInUse(const JPPlacementsHolder& h, const JPJob* job) const;

    // A file's full path, as boards and panels are known by.
    static std::string canonical(const std::string& path);

private:
    bool convertLegacyJob(JPJob& job, std::string& error);
    static void restoreJobSettings(JPJob& job, JPPanelLocation& l);
    static void saveJobSettings(JPJob& job, JPPanelLocation& l);
    std::shared_ptr<JPPanel> loadPanel(const std::string& path, std::string& error);

    std::string                                          m_directory;
    std::vector<std::shared_ptr<JPPart>>                 m_parts;
    std::unordered_map<std::string, std::shared_ptr<JPPart>>    m_partsById;
    std::vector<std::shared_ptr<JPPackage>>              m_packages;
    std::unordered_map<std::string, std::shared_ptr<JPPackage>> m_packagesById;
    std::vector<JPVisionSettings>                        m_vision;
    std::vector<std::shared_ptr<JPBoard>>                m_boards;
    std::vector<std::shared_ptr<JPPanel>>                m_panels;
};

} // inline namespace jf
