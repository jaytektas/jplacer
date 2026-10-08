// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPBoardPad.h"
#include "JPBoardPart.h"
#include "JPBoardRevision.h"
#include "JPPlacementsHolder.h"
#include "JPRevisionCarry.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <j/config/Json.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

inline namespace jf {

// A board, as OpenPnP's, with its own parts list (DESIGN.md, Board): each
// placement names one of the board's parts, which is a library part, the
// board's own, or not yet either. jplacer keeps a board in a JSON file
// (kExtension) that carries all of it; an OpenPnP board file is read (its
// placements' part ids become its board parts) and never written over.
class JPBoard : public JPPlacementsHolder {
public:
    static constexpr const char* kExtension = ".jpboard";
    static constexpr const char* kFormat = "jplacer-board";

    JPBoard() = default;
    Kind kind() const override { return Kind::Board; }
    std::shared_ptr<JPPlacementsHolder> instance() const override;

    std::vector<JPBoardPad> solderPastePads;
    // The OpenPnP board file this one was first saved from (JPConfiguration::saveBoard); empty: none. That
    // file opened is this one (what names the old file follows it).
    std::string convertedFrom;
    // The files it was imported from, each as kept (JPImportSource::provenance): what they said, every row,
    // and how their columns were read. Added to by each import.
    std::vector<JJson> provenance;

    // The board's parts. One list, the definition's: its instances (a job's boards) share it.
    std::vector<JPBoardPart>&       parts() { return *m_parts; }
    const std::vector<JPBoardPart>& parts() const { return *m_parts; }
    JPBoardPart*       part(const std::string& key);
    const JPBoardPart* part(const std::string& key) const;
    // A key no part of the board has.
    std::string newPartKey() const;
    // The board part matched to the library part `libraryId`, made when there is none; its key.
    std::string useLibraryPart(const std::string& libraryId);
    // A parts list of its own (a copy of the one it shares): for a board copied to be a board of its own.
    void ownParts() { m_parts = std::make_shared<std::vector<JPBoardPart>>(*m_parts); }
    // Another board's parts brought into this one (an import merged into it): one this board already has
    // (the same part named) kept as it is, with its choice; the rest added, a board's own part's id scoped
    // to this board (scopeName()/id). Each of `from`'s keys to this board's, for its placements.
    std::map<std::string, std::string> takeParts(const JPBoard& from);
    // What scopes this board's own parts' ids: its file's name without its extension, else its name.
    std::string scopeName() const;
    // A board's own part from elsewhere (an import) made this board's: its part and package copies of its own,
    // their ids scoped to this board.
    void scopeOwn(JPBoardPart& p) const;
    // A placement's part chosen by hand, a library part: its board part matched to it when the placement is
    // its only one, else (the others keep theirs) a board part of its own, the same fields, matched to it;
    // what the files said kept either way. The key the placement is to name (the caller sets it, as an edit).
    std::string matchPlacement(const std::string& placementId, const std::string& libraryId);
    // A board part of the placement's own: its own when no other placement shares it, else a copy (the same
    // fields and choice) the others do not share. Its key (the caller sets the placement to it, as an edit).
    std::string splitPlacement(const std::string& placementId);
    // A board part made the board's own: a part (and, `libraryPackage` null, a package of its own) from what
    // the files said: its name, its footprint, its height; ids scoped to the board.
    void makeOwn(const std::string& key, const JPPackage* libraryPackage);
    // The placements naming a board part.
    std::vector<std::string> placementsOf(const std::string& key) const;
    // The parts no placement names, taken out.
    void dropUnusedParts();
    // Each placement's part id set from its board part (the one place it is set from).
    void syncParts();
    // An OpenPnP board's placements' part ids made board parts: matched where `inLibrary` knows the id,
    // else unmatched (a part the board names and the library has not).
    void partsFromPlacements(const std::function<bool(const std::string&)>& inLibrary);

    // Its revisions (DESIGN.md, Board revisions), oldest first; none until the first upgrade. The one shown is
    // revision(): its files, placements and parts are the board's (provenance, placements, parts()); its
    // entry here keeps only its label, when it was made and its summary.
    const std::vector<JPBoardRevision>& revisions() const { return m_revisions; }
    size_t revision() const { return m_revision; }
    // The shown one's label; empty: no revisions kept.
    std::string revisionLabel() const;
    // A revision, complete (the one shown as it is now).
    JPBoardRevision revisionAt(size_t i) const;
    // Each placement without an identity across revisions given one.
    void giveIdentities();
    // A revision made by an upgrade (JPBoardUpgrade), added and shown. The one shown until now is kept; when the
    // board kept none, as its first, labelled `currentLabel`.
    void addRevision(JPBoardRevision r, const std::string& currentLabel);
    // Revision `i` shown, the one shown until now kept as it is: nothing else changed (the revision a job was
    // saved with, shown as it is opened).
    void showRevision(size_t i);
    // The revision labelled `label`; revisions().size() when there is none.
    size_t revisionNamed(const std::string& label) const;
    // Revision `i` shown, the one shown until now kept as it is; given that one's work where it still holds
    // (JPRevisionCarry): for a placement of the same identity, side, CAD position and rotation, and part, its
    // verified rotation and mark (verified later than here), and its part's choice.
    JPRevisionCarry switchRevision(size_t i);
    // The work carried undone: the revision shown as it was before.
    void undoCarry(const JPRevisionCarry& carry);
    // This, an instance of `def`, showing the revision `def` shows: its placements, each keeping what a job
    // set for it (whether it is enabled, its error handling), found by its identity, else its id.
    void followRevision(const JPBoard& def);

    static JPBoard fromXml(const JPXmlElement& root);
    JPXmlNode toXml() const;
    // jplacer's board file.
    static JPBoard fromJson(const JJson& j);
    JJson toJson() const;
    // Whether `path` is one of jplacer's board files (by its extension).
    static bool isJplacerFile(const std::string& path);

private:
    // The one shown kept in its entry, complete; revision `i`'s taken from its entry and shown.
    void keepShown();
    void show(size_t i);

    std::vector<JPBoardRevision>              m_revisions;
    size_t                                    m_revision = 0;
    std::shared_ptr<std::vector<JPBoardPart>> m_parts = std::make_shared<std::vector<JPBoardPart>>();
};

} // inline namespace jf
