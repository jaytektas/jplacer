// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoard.h"
#include "JPBoardLocation.h"
#include "JPJob.h"
#include "JPFeeder.h"
#include "JPPhotonProperties.h"
#include "JPDropBoxes.h"
#include "JPSlotBanks.h"
#include "JPPackage.h"
#include "JPPanel.h"
#include "JPPanelLocation.h"
#include "JPLibraryStore.h"
#include "JPRunStore.h"
#include "JPStockStore.h"
#include "JPPart.h"
#include "JPVisionSettings.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

inline namespace jf {

// What OpenPnP's Configuration holds besides the machine: the parts and
// packages (parts.xml, packages.xml) in one folder, and the boards and panels
// open (the open job's, and those opened beside it: none at start; a job
// brings its own); and loading and saving jobs, boards and panels. Parts and packages are found by id
// without regard to case, and keep the order they were added in.
class JPConfiguration {
public:
    static constexpr const char* kPartsFile    = "parts.xml";
    static constexpr const char* kPackagesFile = "packages.xml";
    // The machine's feeders, as OpenPnP's machine.xml has them (<feeders>).
    static constexpr const char* kFeedersFile  = "feeders.xml";
    static constexpr const char* kVisionFile   = "vision-settings.xml";
    // What feeders keep on the machine (slot feeders' banks, Photon feeders'
    // slots), as machine.xml's <properties> entries for them.
    static constexpr const char* kMachinePropertiesFile = "machine-properties.xml";

    explicit JPConfiguration(std::string directory);

    const std::string& directory() const { return m_directory; }
    // Where OpenPnP's default packages, parts and vision settings are
    // (empty: none): a file of them not in directory() is read from there,
    // as OpenPnP's Configuration.load does, and tookDefaults() says so.
    void setDefaults(std::string directory) { m_defaults = std::move(directory); }
    bool tookDefaults() const { return m_tookDefaults; }
    // Reads what is there (a missing file is an empty list, or the
    // default's); what it brought in or could not do, in `problems`.
    bool load(std::vector<std::string>& problems, std::string& error);
    // The library as JSON, to read (a diagnostics file): each part with its id, UUID, value, description,
    // datasheet, identifiers, AKAs and OpenPnP fields; each package with its AKAs and OpenPnP fields.
    JJson libraryJson() const;
    // The library, every object a copy of its own (parts, packages, footprints, manufacturers): to go back to.
    JPLibraryStore::Contents librarySnapshot() const;
    // The library made `snapshot` again. An object of the same uuid is changed in place (what points at it keeps
    // pointing at it), the others added, and those not in it taken away; the order is the snapshot's.
    void restoreLibrary(const JPLibraryStore::Contents& snapshot);
    // Writes the parts, packages, vision settings and feeders.
    bool save(std::string& error) const;

    // The library's parts (parts.xml): every job draws from them. A board's own parts are not among them.
    const std::vector<std::shared_ptr<JPPart>>& parts() const { return m_parts; }
    // A part to place with, by id: the library's, else a loaded board's own (JPBoardPart, Local).
    JPPart* part(const std::string& id) const;
    // The library's part of that id only, or null.
    JPPart* libraryPart(const std::string& id) const;
    // The library's part a matched board part is: by its id, else (renamed since) by its uuid; null when the
    // library has neither (a board from another machine's library).
    JPPart* libraryPartFor(const JPBoardPart& bp) const;
    // A matched board part's copy taken from the library's part (and package) as it is now, with its
    // fingerprint and uuid. Nothing when the library has no such part.
    void takeCopy(JPBoardPart& bp) const;
    // Whether a matched board part's copy is not the library's part as it is now (changed since, or not in
    // this library at all): to be reviewed.
    bool differs(const JPBoardPart& bp) const;
    // The library's part made the board's copy (its placing fields; its names, uuid, id kept).
    void giveCopy(const JPBoardPart& bp);
    // The library footprint a board part's part is placed with: the one its CAD footprint names, of the part's
    // package, else that package's first; null when the package has none.
    const JPLibraryFootprint* footprintFor(const JPBoardPart& bp, const JPPart& part) const;
    // Replaces one of the same id, where it was.
    void addPart(std::shared_ptr<JPPart> part);
    void removePart(const std::string& id);

    const std::vector<std::shared_ptr<JPPackage>>& packages() const { return m_packages; }
    // A package by id: the library's, else a loaded board's own.
    JPPackage* package(const std::string& id) const;
    // The library's package of that id only, or null.
    JPPackage* libraryPackage(const std::string& id) const;
    // The manufacturers the library knows, with their other names.
    const std::vector<JPManufacturer>& manufacturers() const { return m_manufacturers; }
    std::vector<JPManufacturer>&       manufacturers() { return m_manufacturers; }
    // The library's stock: its lots and their ledger (not open for a configuration read from no folder).
    JPStockStore& stock() const { return m_stock; }
    // The runs of jobs (runs.db beside the library; not open for a configuration read from no folder).
    JPRunStore& runs() const { return m_runs; }
    // A manufacturer's name as the library has it, by its name or one of its others (any case); the name as
    // given when the library knows it by neither.
    std::string manufacturerName(const std::string& name) const;
    // Whether two names name one manufacturer (either unknown: when they are the same name).
    bool sameManufacturer(const std::string& a, const std::string& b) const;
    // The library's package a CAD footprint names: by its id, else by a footprint of it that name names
    // (footprintNamed); null when none.
    JPPackage* packageNamed(const std::string& footprint) const;

    // The library's footprints (land patterns of its packages: JPLibraryFootprint).
    const std::vector<std::shared_ptr<JPLibraryFootprint>>& footprints() const { return m_footprints; }
    JPLibraryFootprint* footprint(const std::string& uuid) const;
    // The footprint a CAD file's name names: by its name or one of its CAD names (any case); null when none.
    JPLibraryFootprint* footprintNamed(const std::string& name) const;
    std::vector<JPLibraryFootprint*> footprintsOf(const std::string& packageId) const;
    // A package's first footprint; with `make`, one made from the package's own (named after it) when it has none.
    JPLibraryFootprint* defaultFootprint(const std::string& packageId, bool make);
    void addFootprint(std::shared_ptr<JPLibraryFootprint> f);
    void removeFootprint(const std::string& uuid);
    void addPackage(std::shared_ptr<JPPackage> package);
    void removePackage(const std::string& id);

    // Vision settings, as named by parts and packages (read; the Vision tab writes them).
    const std::vector<JPVisionSettings>& visionSettings() const { return m_vision; }
    std::vector<JPVisionSettings>&       visionSettings() { return m_vision; }
    const JPVisionSettings* visionSettings(const std::string& id) const;
    JPVisionSettings*       visionSettings(const std::string& id);
    void addVisionSettings(JPVisionSettings v) { m_vision.push_back(std::move(v)); }
    void removeVisionSettings(const std::string& id);
    // What uses a vision settings, as OpenPnP's Assigned To lists it: the
    // stock settings themselves, the machine's (named `machineName` when its
    // id is `machineDefaultId`), then packages, then parts, by id.
    std::vector<std::string> visionUsedIn(const JPVisionSettings& v, const std::string& machineDefaultId,
                                          const std::string& machineName) const;
    // The settings a part's bottom vision or fiducials use (OpenPnP's
    // getInheritedVisionSettings): the part's, else its package's, else the
    // machine's default; none when that names nothing.
    const JPVisionSettings* inheritedVision(const JPPart& part, JPVisionSettings::Kind kind,
                                            const std::string& machineDefaultId) const;
    // Placements on the known boards that use the part (OpenPnP's Placements column).
    int placementCount(const std::string& partId) const;

    // The feeders, in order.
    std::vector<JPFeeder>&       feeders() { return m_feeders; }
    const std::vector<JPFeeder>& feeders() const { return m_feeders; }
    JPFeeder*       feeder(const std::string& id);
    const JPFeeder* feeder(const std::string& id) const;
    // A feeder added after the others; one taken away.
    JPFeeder& addFeeder(JPFeeder f);
    void      removeFeeder(const std::string& id);
    // The banks a slot feeder kind's slots are loaded from (made with one
    // bank, "Default", when there are none).
    JPSlotBanks& slotBanks(const std::string& typeName);
    // Each slot feeder's load found again from its bank-id and feeder-id
    // (JPFeeder::slotLoad), after its banks or its attributes changed: one
    // without a bank, or naming one not there, is in the last bank; a feeder
    // loaded in two slots stays in the later one (as OpenPnP's).
    void resolveSlots();
    // Load the bank's feeder into a slot (none: empty it), taking it out of
    // any other slot; a slot's bank chosen (what was loaded taken out).
    void loadSlot(const std::string& slotId, const std::string& bankFeederId);
    void setSlotBank(const std::string& slotId, const std::string& bankId);
    // A slot's bank: its own, else the last.
    std::string slotBankId(const JPFeeder& slot);
    // What the Photon feeders keep on the machine.
    // The heap feeders' drop boxes (the machine property OpenPnP keeps them in).
    JPDropBoxes&       dropBoxes() { return m_dropBoxes; }
    const JPDropBoxes& dropBoxes() const { return m_dropBoxes; }
    JPPhotonProperties&       photon() { return m_photon; }
    // Each Photon feeder's slot location found again (JPFeeder::photonSlotLocation).
    void resolvePhoton();
    // A Photon feeder's slot address (none: not found); another feeder at
    // that address loses it (and is to be initialized again).
    void setPhotonSlot(const std::string& feederId, std::optional<int> address);
    // The Photon feeder of a hardware id (empty: one not yet given one).
    JPFeeder* photonFeeder(const std::string& hardwareId);
    // The feeders of an OpenPnP machine.xml, in place of these (Machine >
    // Import OpenPnP Machine); how many, or -1 (and why) when it cannot be read.
    int importFeeders(const std::string& machineXml, std::string& error);
    // The feeder to take a part from, as OpenPnP's FeederUtils.findFeeder:
    // of the enabled feeders holding it, those of the highest priority, and
    // of those the one whose pick location is closest to `datum` (the
    // head's camera; none: the first). None when no enabled feeder has it.
    JPFeeder* findFeeder(const std::string& partId, const std::optional<JPLocation>& datum);
    // Whether an enabled feeder holds the part (a placement's Missing Feeder).
    bool hasFeeder(const std::string& partId) const;
    // How many feeders hold the part (the Parts tab's Feeders column).
    int feederCount(const std::string& partId) const;

    const std::vector<std::shared_ptr<JPBoard>>& boards() const { return m_boards; }
    const std::vector<std::shared_ptr<JPPanel>>& panels() const { return m_panels; }
    // The board kept in `path`: one already known, else read (and known
    // from now on); a file that is not there is made, an empty board.
    std::shared_ptr<JPBoard> board(const std::string& path, std::string& error);
    std::shared_ptr<JPPanel> panel(const std::string& path, std::string& error);
    void addBoard(std::shared_ptr<JPBoard> board);
    void addPanel(std::shared_ptr<JPPanel> panel);
    void removeBoard(const JPBoard* board);
    // Every open board and panel closed (another job is opened, or none): what was open, to keep alive while
    // views still point at it, or to have open again (reopen) when the next job cannot be read.
    struct Open {
        std::vector<std::shared_ptr<JPBoard>> boards;
        std::vector<std::shared_ptr<JPPanel>> panels;
    };
    Open closeAll();
    void reopen(Open open);
    void removePanel(const JPPanel* panel);
    // Written as jplacer's board file; an OpenPnP one (read from XML) gets a file of its own beside it, its
    // old path in `movedFrom` (panels naming it follow; a job's locations are the job's to follow).
    bool saveBoard(JPBoard& board, std::string& error, std::string* movedFrom = nullptr);
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
    // OpenPnP's parts.xml and packages.xml in the folder, as when last changed (a changed one is looked at again).
    std::string openPnpStamp() const;
    // The library as the store keeps it.
    JPLibraryStore::Contents contents() const;
    bool convertLegacyJob(JPJob& job, std::string& error);
    static void restoreJobSettings(JPJob& job, JPPanelLocation& l);
    static void saveJobSettings(JPJob& job, JPPanelLocation& l);
    std::shared_ptr<JPPanel> loadPanel(const std::string& path, std::string& error);

    std::string                                          m_directory;
    std::vector<std::shared_ptr<JPLibraryFootprint>>     m_footprints;
    std::vector<JPManufacturer>                          m_manufacturers;
    // The library's file (library.db): written on save (from a const save, so mutable).
    mutable JPLibraryStore                                m_library;
    // The library's stock (its lots and ledger, in the same file): written as each change is made.
    mutable JPStockStore                                  m_stock;
    mutable JPRunStore                                    m_runs;
    std::string                                          m_defaults;
    bool                                                 m_tookDefaults = false;
    std::vector<std::shared_ptr<JPPart>>                 m_parts;
    std::unordered_map<std::string, std::shared_ptr<JPPart>>    m_partsById;
    std::vector<std::shared_ptr<JPPackage>>              m_packages;
    std::unordered_map<std::string, std::shared_ptr<JPPackage>> m_packagesById;
    std::vector<JPVisionSettings>                        m_vision;
    std::vector<JPFeeder>                                m_feeders;
    std::vector<JPSlotBanks>                             m_slotBanks;
    JPPhotonProperties                                   m_photon;
    JPDropBoxes                                          m_dropBoxes;
    std::vector<std::shared_ptr<JPBoard>>                m_boards;
    std::vector<std::shared_ptr<JPPanel>>                m_panels;
};

} // inline namespace jf
