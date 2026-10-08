// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPConfiguration.h"

#include "JPLibraryJson.h"
#include "JPXmlValues.h"

#include "common/JPUuid.h"
#include "common/JPlacerLog.h"

#include "openpnp/JPXmlJson.h"
#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <tuple>

inline namespace jf {

namespace fs = std::filesystem;

namespace {

std::string upper(const std::string& s) {
    std::string out;
    for (const char c : s) out += char(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool exists(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p, ec);
}

// A board or panel's file, as OpenPnP finds it: as named, beside its
// parent's file, beside the job's.
std::string find(const std::string& name, const std::string& parentFile, const std::string& jobFile) {
    if (exists(name)) return name;
    if (!parentFile.empty()) {
        const std::string p = (fs::path(parentFile).parent_path() / name).string();
        if (exists(p)) return p;
    }
    if (!jobFile.empty()) {
        const std::string p = (fs::path(jobFile).parent_path() / name).string();
        if (exists(p)) return p;
    }
    return {};
}

} // namespace

JPConfiguration::JPConfiguration(std::string directory) : m_directory(std::move(directory)) {}

std::string JPConfiguration::canonical(const std::string& path) {
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::absolute(path, ec), ec);
    return ec ? path : p.string();
}

bool JPConfiguration::load(std::vector<std::string>& problems, std::string& error) {
    const fs::path dir(m_directory);
    auto read = [&error](const fs::path& p, JPXmlElement& root) {
        return !exists(p.string()) || JPXmlReader::read(p.string(), root, error);
    };
    // Not there yet: OpenPnP's default, when there is one.
    auto orDefault = [this, &dir](const char* file) {
        if (m_defaults.empty() || exists((dir / file).string()) || !exists((fs::path(m_defaults) / file).string()))
            return dir / file;
        m_tookDefaults = true;
        return fs::path(m_defaults) / file;
    };
    JPXmlElement packages, parts, boards, panels, vision, feeders, banks;
    if (!read(orDefault(kPackagesFile), packages) || !read(orDefault(kPartsFile), parts) || !read(dir / kBoardsFile, boards)
        || !read(dir / kPanelsFile, panels) || !read(orDefault(kVisionFile), vision) || !read(dir / kFeedersFile, feeders)
        || !read(dir / kMachinePropertiesFile, banks))
        return false;
    for (const JPXmlElement& e : feeders.children)
        if (e.name == "feeder") m_feeders.push_back(JPFeeder::fromXml(e));
    for (const JPXmlElement& e : banks.children) {
        if (auto b = JPSlotBanks::fromXml(e)) m_slotBanks.push_back(std::move(*b));
        else if (auto d = JPDropBoxes::fromXml(e)) m_dropBoxes = std::move(*d);
        else m_photon.take(e);
    }
    resolveSlots();
    resolvePhoton();
    for (const JPXmlElement& e : vision.children)
        if (e.name == "vision-settings") m_vision.push_back(JPVisionSettings::fromXml(e));
    // The library: library.db, else made from OpenPnP's parts.xml and packages.xml (left as they are); then any of
    // those files' parts and packages it lacks, when the files changed since it last looked (OpenPnP's copied in).
    const fs::path libraryFile = dir / JPLibraryStore::kFile;
    const bool hadLibrary = exists(libraryFile.string());
    if (!m_library.open(libraryFile.string(), error)) return false;
    bool migrated = false;
    if (hadLibrary) {
        JPLibraryStore::Contents in;
        if (!m_library.load(in, error)) return false;
        for (auto& k : in.packages) addPackage(std::move(k));
        for (auto& p : in.parts) addPart(std::move(p));
        for (auto& f : in.footprints) addFootprint(std::move(f));
        m_manufacturers = std::move(in.manufacturers);
        // A library of before footprints: the CAD names its packages were known by are their footprints' names.
        for (const auto& [packageUuid, name] : in.packageNames)
            for (const auto& k : m_packages)
                if (k->uuid == packageUuid)
                    if (JPLibraryFootprint* f = defaultFootprint(k->id, true);
                        std::find(f->cadNames.begin(), f->cadNames.end(), name) == f->cadNames.end())
                        f->cadNames.push_back(name);
        migrated = !in.packageNames.empty();
    }
    const std::string stamp = openPnpStamp();
    if (!hadLibrary || migrated || stamp != m_library.meta("openpnpFiles")) {
        int added = 0;
        for (const JPXmlElement& e : packages.children)
            if (e.name == "package" && !libraryPackage(e.attr("id"))) {
                addPackage(std::make_shared<JPPackage>(JPPackage::fromXml(e)));
                ++added;
            }
        for (const JPXmlElement& e : parts.children)
            if (e.name == "part" && !libraryPart(e.attr("id"))) {
                addPart(std::make_shared<JPPart>(JPPart::fromXml(e)));
                ++added;
            }
        if (hadLibrary && added > 0)
            problems.push_back(std::to_string(added) + " part(s) and package(s) of OpenPnP's parts.xml and packages.xml "
                               "the library did not have were added to it");
        std::string why;
        if (!m_library.save(contents(), why) || !m_library.setMeta("openpnpFiles", stamp)) {
            error = why;
            return false;
        }
    }
    for (const JPXmlElement& e : boards.children) {
        const std::string path = JPXmlValues::text(e);
        std::string why;
        if (!exists(path)) problems.push_back("Could not load board " + path + ", file is missing.");
        else if (!board(path, why)) problems.push_back("Could not load board " + path + ", file may be corrupt: " + why);
    }
    for (const JPXmlElement& e : panels.children) {
        const std::string path = JPXmlValues::text(e);
        std::string why;
        if (!exists(path)) problems.push_back("Could not load panel " + path + ", file is missing.");
        else if (!panel(path, why)) problems.push_back("Could not load panel " + path + ", file may be corrupt: " + why);
    }
    return true;
}

bool JPConfiguration::save(std::string& error) const {
    const fs::path dir(m_directory);
    // The library's parts and packages, to library.db (OpenPnP's parts.xml and packages.xml are not written).
    if (!m_library.isOpen() && !m_library.open((dir / JPLibraryStore::kFile).string(), error)) return false;
    if (!m_library.save(contents(), error)) return false;
    JPXmlNode boards("openpnp-boards");
    for (const auto& b : m_boards) boards.add(JPXmlNode("board")).text = b->file;
    JPXmlNode panels("openpnp-panels");
    for (const auto& p : m_panels) panels.add(JPXmlNode("panel")).text = p->file;
    JPXmlNode feeders("feeders");
    for (const JPFeeder& f : m_feeders) feeders.add(f.toXml());
    if (!JPXmlWriter::write((dir / kFeedersFile).string(), feeders, error)) return false;
    JPXmlNode properties("properties");
    for (const JPSlotBanks& b : m_slotBanks) properties.add(b.toXml());
    for (JPXmlNode& e : m_photon.toXml()) properties.add(std::move(e));
    properties.add(m_dropBoxes.toXml());
    if (!JPXmlWriter::write((dir / kMachinePropertiesFile).string(), properties, error)) return false;
    JPXmlNode vision("openpnp-vision-settings");
    for (const JPVisionSettings& v : m_vision) vision.add(v.toXml());
    if (!JPXmlWriter::write((dir / kVisionFile).string(), vision, error)) return false;
    return JPXmlWriter::write((dir / kBoardsFile).string(), boards, error)
        && JPXmlWriter::write((dir / kPanelsFile).string(), panels, error);
}

int JPConfiguration::importFeeders(const std::string& machineXml, std::string& error) {
    JPXmlElement root;
    if (!JPXmlReader::read(machineXml, root, error)) return -1;
    const JPXmlElement* machine = root.child("machine");
    const JPXmlElement* feeders = machine ? machine->child("feeders") : nullptr;
    m_feeders.clear();
    if (feeders)
        for (const JPXmlElement& e : feeders->children)
            if (e.name == "feeder") m_feeders.push_back(JPFeeder::fromXml(e));
    // The slot feeders' banks and the Photon feeders' slots, from the machine's properties.
    m_slotBanks.clear();
    m_photon = JPPhotonProperties();
    m_dropBoxes = JPDropBoxes();
    if (const JPXmlElement* properties = machine ? machine->child("properties") : nullptr)
        for (const JPXmlElement& e : properties->children) {
            if (e.name != "entry") continue;
            if (auto b = JPSlotBanks::fromXml(e)) m_slotBanks.push_back(std::move(*b));
            else if (auto d = JPDropBoxes::fromXml(e)) m_dropBoxes = std::move(*d);
            else m_photon.take(e);
        }
    resolveSlots();
    resolvePhoton();
    return int(m_feeders.size());
}

JPSlotBanks& JPConfiguration::slotBanks(const std::string& typeName) {
    const std::string key = JPSlotBanks::keyFor(typeName);
    for (JPSlotBanks& b : m_slotBanks)
        if (b.key() == key) return b;
    m_slotBanks.emplace_back(key);
    return m_slotBanks.back();
}

std::string JPConfiguration::slotBankId(const JPFeeder& slot) {
    const std::vector<JPSlotBanks::Bank> banks = slotBanks(slot.typeName()).banks();
    const std::string own = slot.text("bank-id");
    for (const auto& b : banks)
        if (b.id == own) return own;
    return banks.empty() ? std::string() : banks.back().id;
}

void JPConfiguration::resolveSlots() {
    // (kind, bank, feeder) of each load, the later slot keeping it.
    std::map<std::tuple<std::string, std::string, std::string>, JPFeeder*> loaded;
    for (JPFeeder& f : m_feeders) {
        if (!f.isSlot()) continue;
        f.slotLoad.reset();
        const std::string bank = slotBankId(f), feederId = f.text("feeder-id");
        const auto feeder = feederId.empty() ? std::nullopt : slotBanks(f.typeName()).feeder(bank, feederId);
        if (!feeder) continue;
        auto& holder = loaded[{ f.typeName(), bank, feederId }];
        if (holder) {
            holder->slotLoad.reset();
            holder->setText("feeder-id", "");
        }
        holder = &f;
        f.slotLoad = JPFeeder::SlotLoad { feeder->name, feeder->partId, feeder->offsets };
    }
}

void JPConfiguration::resolvePhoton() {
    for (JPFeeder& f : m_feeders)
        if (f.isPhoton()) f.photonSlotLocation = f.photonSlot ? m_photon.slotLocation(*f.photonSlot) : std::nullopt;
}

void JPConfiguration::setPhotonSlot(const std::string& feederId, std::optional<int> address) {
    if (address)
        for (JPFeeder& other : m_feeders)
            if (other.isPhoton() && other.id() != feederId && other.photonSlot == address) {
                other.photonSlot.reset();
                other.photonInitialized = false;
            }
    if (JPFeeder* f = feeder(feederId)) f->photonSlot = address;
    resolvePhoton();
}

JPFeeder* JPConfiguration::photonFeeder(const std::string& hardwareId) {
    for (JPFeeder& f : m_feeders)
        if (f.isPhoton() && f.text("hardware-id") == hardwareId) return &f;
    return nullptr;
}

void JPConfiguration::loadSlot(const std::string& slotId, const std::string& bankFeederId) {
    JPFeeder* slot = feeder(slotId);
    if (!slot) return;
    const std::string bank = slotBankId(*slot);
    if (!bankFeederId.empty())
        for (JPFeeder& other : m_feeders)
            if (&other != slot && other.typeName() == slot->typeName() && other.text("feeder-id") == bankFeederId
                && slotBankId(other) == bank)
                other.setText("feeder-id", "");
    slot->setText("bank-id", bank);
    slot->setText("feeder-id", bankFeederId);
    resolveSlots();
}

void JPConfiguration::setSlotBank(const std::string& slotId, const std::string& bankId) {
    JPFeeder* slot = feeder(slotId);
    if (!slot) return;
    slot->setText("bank-id", bankId);
    slot->setText("feeder-id", "");
    resolveSlots();
}

JPFeeder& JPConfiguration::addFeeder(JPFeeder f) {
    m_feeders.push_back(std::move(f));
    return m_feeders.back();
}

void JPConfiguration::removeFeeder(const std::string& id) {
    std::erase_if(m_feeders, [&id](const JPFeeder& f) { return f.id() == id; });
}

const JPFeeder* JPConfiguration::feeder(const std::string& id) const {
    for (const JPFeeder& f : m_feeders)
        if (f.id() == id) return &f;
    return nullptr;
}

JPFeeder* JPConfiguration::feeder(const std::string& id) {
    for (JPFeeder& f : m_feeders)
        if (f.id() == id) return &f;
    return nullptr;
}

JPFeeder* JPConfiguration::findFeeder(const std::string& partId, const std::optional<JPLocation>& datum) {
    std::vector<JPFeeder*> found;
    JPFeeder::Priority highest = JPFeeder::Priority::Low;
    for (JPFeeder& f : m_feeders)
        if (f.enabled() && f.partId() == partId) {
            found.push_back(&f);
            if (int(f.priority()) < int(highest)) highest = f.priority();
        }
    std::erase_if(found, [highest](const JPFeeder* f) { return f->priority() != highest; });
    JPFeeder* closest = nullptr;
    double closestCost = 0;
    for (JPFeeder* f : found) {
        double cost = 0;
        if (datum) {
            const JPLocation at = f->pickLocation().value_or(f->location()).convertToUnits(JPLengthUnit::Millimeters);
            const JPLocation d = datum->convertToUnits(JPLengthUnit::Millimeters);
            cost = std::hypot(at.x() - d.x(), at.y() - d.y(), at.z() - d.z());
        }
        if (!closest || cost < closestCost) {
            closest = f;
            closestCost = cost;
        }
    }
    return closest;
}

bool JPConfiguration::hasFeeder(const std::string& partId) const {
    const std::string k = upper(partId);
    for (const JPFeeder& f : m_feeders)
        if (f.enabled() && upper(f.partId()) == k) return true;
    return false;
}

int JPConfiguration::feederCount(const std::string& partId) const {
    const std::string k = upper(partId);
    int n = 0;
    for (const JPFeeder& f : m_feeders)
        if (upper(f.partId()) == k) ++n;
    return n;
}

JPVisionSettings* JPConfiguration::visionSettings(const std::string& id) {
    for (JPVisionSettings& v : m_vision)
        if (v.id == id) return &v;
    return nullptr;
}

void JPConfiguration::removeVisionSettings(const std::string& id) {
    std::erase_if(m_vision, [&id](const JPVisionSettings& v) { return v.id == id; });
}

std::vector<std::string> JPConfiguration::visionUsedIn(const JPVisionSettings& v, const std::string& machineDefaultId,
                                                       const std::string& machineName) const {
    std::vector<std::string> out;
    if (v.isStock()) out.push_back(v.name);
    if (v.id == machineDefaultId) out.push_back(machineName);
    const bool bottom = v.kind == JPVisionSettings::Kind::Bottom;
    std::vector<std::string> packages, parts;
    for (const auto& p : m_packages)
        if ((bottom ? p->bottomVisionId : p->fiducialVisionId) == v.id) packages.push_back(p->id);
    for (const auto& p : m_parts)
        if ((bottom ? p->bottomVisionId : p->fiducialVisionId) == v.id) parts.push_back(p->id);
    std::sort(packages.begin(), packages.end());
    std::sort(parts.begin(), parts.end());
    out.insert(out.end(), packages.begin(), packages.end());
    out.insert(out.end(), parts.begin(), parts.end());
    return out;
}

const JPVisionSettings* JPConfiguration::inheritedVision(const JPPart& part, JPVisionSettings::Kind kind,
                                                         const std::string& machineDefaultId) const {
    const bool bottom = kind == JPVisionSettings::Kind::Bottom;
    if (const JPVisionSettings* v = visionSettings(bottom ? part.bottomVisionId : part.fiducialVisionId)) return v;
    if (const JPPackage* pkg = package(part.packageId))
        if (const JPVisionSettings* v = visionSettings(bottom ? pkg->bottomVisionId : pkg->fiducialVisionId)) return v;
    return visionSettings(machineDefaultId);
}

const JPVisionSettings* JPConfiguration::visionSettings(const std::string& id) const {
    const std::string k = upper(id);
    for (const JPVisionSettings& v : m_vision)
        if (upper(v.id) == k) return &v;
    return nullptr;
}

int JPConfiguration::placementCount(const std::string& partId) const {
    const std::string k = upper(partId);
    int n = 0;
    for (const auto& b : m_boards)
        for (const JPPlacement& p : b->placements)
            if (upper(p.partId) == k) ++n;
    return n;
}

JPPart* JPConfiguration::part(const std::string& id) const {
    const auto it = m_partsById.find(upper(id));
    if (it != m_partsById.end()) return it->second.get();
    // Not the library's: a board's own part, by its (board-scoped) id.
    const std::string k = upper(id);
    for (const auto& b : m_boards)
        for (const JPBoardPart& bp : b->parts()) {
            if (bp.state == JPBoardPart::State::Local && bp.localPart && upper(bp.localPart->id) == k) return bp.localPart.get();
            // A library part this library lacks (a board from another's): the board's copy of it.
            if (bp.state == JPBoardPart::State::Matched && bp.copyPart && upper(bp.copyPart->id) == k) return bp.copyPart.get();
        }
    return nullptr;
}

JPPart* JPConfiguration::libraryPart(const std::string& id) const {
    const auto it = m_partsById.find(upper(id));
    return it == m_partsById.end() ? nullptr : it->second.get();
}

JPPackage* JPConfiguration::libraryPackage(const std::string& id) const {
    const auto it = m_packagesById.find(upper(id));
    return it == m_packagesById.end() ? nullptr : it->second.get();
}

JJson JPConfiguration::libraryJson() const {
    JJson j = JJson::object();
    JJson parts = JJson::array();
    for (const auto& p : m_parts) parts.push(JPLibraryJson::part(*p));
    j["parts"] = parts;
    JJson packages = JJson::array();
    for (const auto& k : m_packages) packages.push(JPLibraryJson::package(*k));
    j["packages"] = packages;
    JJson footprints = JJson::array();
    for (const auto& f : m_footprints) footprints.push(JPLibraryJson::footprint(*f));
    j["footprints"] = footprints;
    JJson makers = JJson::array();
    for (const JPManufacturer& m : m_manufacturers) {
        JJson o = JJson::object();
        o["name"] = m.name;
        JJson akas = JJson::array();
        for (const std::string& a : m.akas) akas.push(JJson(a));
        o["akas"] = akas;
        makers.push(o);
    }
    j["manufacturers"] = makers;
    return j;
}

std::string JPConfiguration::openPnpStamp() const {
    // When OpenPnP's files in the folder were last changed (each's time, or none): one changed, a new look.
    std::string s;
    for (const char* f : { kPartsFile, kPackagesFile }) {
        std::error_code ec;
        const auto t = fs::last_write_time(fs::path(m_directory) / f, ec);
        s += std::string(f) + "=" + (ec ? std::string("none") : std::to_string(t.time_since_epoch().count())) + ";";
    }
    return s;
}

JPPart* JPConfiguration::libraryPartFor(const JPBoardPart& bp) const {
    if (JPPart* p = libraryPart(bp.libraryPartId)) return p;
    if (!bp.libraryUuid.empty())
        for (const auto& p : m_parts)
            if (p->uuid == bp.libraryUuid) return p.get();
    return nullptr;
}

void JPConfiguration::takeCopy(JPBoardPart& bp) const {
    const JPPart* p = libraryPartFor(bp);
    if (!p) return;
    bp.libraryPartId = p->id;
    bp.libraryUuid = p->uuid;
    bp.copyPart = std::make_shared<JPPart>(*p);
    const JPPackage* k = libraryPackage(p->packageId);
    bp.copyPackage = k ? std::make_shared<JPPackage>(*k) : nullptr;
    const JPLibraryFootprint* f = footprintFor(bp, *p);
    bp.copyFootprint = f ? std::make_shared<JPLibraryFootprint>(*f) : nullptr;
    bp.fingerprint = JPLibraryJson::fingerprint(*p, k, f);
}

const JPLibraryFootprint* JPConfiguration::footprintFor(const JPBoardPart& bp, const JPPart& p) const {
    // The one its CAD footprint names, when it is of the part's package; else the package's first.
    const std::string cad = !bp.field("footprint").empty() ? bp.field("footprint") : bp.field("package");
    if (const JPLibraryFootprint* f = footprintNamed(cad); f && upper(f->packageId) == upper(p.packageId)) return f;
    const auto of = footprintsOf(p.packageId);
    return of.empty() ? nullptr : of.front();
}

bool JPConfiguration::differs(const JPBoardPart& bp) const {
    if (bp.state != JPBoardPart::State::Matched || !bp.copyPart) return false;
    const JPPart* p = libraryPartFor(bp);
    return !p || JPLibraryJson::fingerprint(*p, libraryPackage(p->packageId), footprintFor(bp, *p)) != bp.fingerprint;
}

void JPConfiguration::giveCopy(const JPBoardPart& bp) {
    if (!bp.copyPart) return;
    JPPart* p = libraryPartFor(bp);
    if (!p) {
        // Not in this library: the copy joins it, as it was.
        auto made = std::make_shared<JPPart>(*bp.copyPart);
        if (bp.copyPackage && !libraryPackage(bp.copyPackage->id)) addPackage(std::make_shared<JPPackage>(*bp.copyPackage));
        if (bp.copyFootprint && !footprint(bp.copyFootprint->uuid)) addFootprint(std::make_shared<JPLibraryFootprint>(*bp.copyFootprint));
        addPart(made);
        return;
    }
    // Its placing fields the copy's; what the library knows it by (id, uuid, names) its own.
    JPPart updated = *bp.copyPart;
    updated.id = p->id;
    updated.uuid = p->uuid;
    updated.identifiers = p->identifiers;
    updated.akas = p->akas;
    *p = updated;
    if (bp.copyPackage)
        if (JPPackage* k = libraryPackage(bp.copyPackage->id)) {
            const std::string uuid = k->uuid;
            *k = *bp.copyPackage;
            k->uuid = uuid;
        }
    // Its footprint's land pattern and rotation (its names the library's).
    if (bp.copyFootprint)
        if (JPLibraryFootprint* f = footprint(bp.copyFootprint->uuid)) {
            f->geometry = bp.copyFootprint->geometry;
            f->zeroRotationDeg = bp.copyFootprint->zeroRotationDeg;
        }
}

std::string JPConfiguration::manufacturerName(const std::string& name) const {
    const std::string want = upper(name);
    for (const JPManufacturer& m : m_manufacturers) {
        if (upper(m.name) == want) return m.name;
        for (const std::string& a : m.akas)
            if (upper(a) == want) return m.name;
    }
    return name;
}

bool JPConfiguration::sameManufacturer(const std::string& a, const std::string& b) const {
    return upper(manufacturerName(a)) == upper(manufacturerName(b));
}

JPPackage* JPConfiguration::packageNamed(const std::string& footprint) const {
    if (footprint.empty()) return nullptr;
    if (JPPackage* k = libraryPackage(footprint)) return k;
    const JPLibraryFootprint* f = footprintNamed(footprint);
    return f ? libraryPackage(f->packageId) : nullptr;
}

JPLibraryFootprint* JPConfiguration::footprint(const std::string& uuid) const {
    for (const auto& f : m_footprints)
        if (f->uuid == uuid) return f.get();
    return nullptr;
}

JPLibraryFootprint* JPConfiguration::footprintNamed(const std::string& name) const {
    if (name.empty()) return nullptr;
    const std::string want = upper(name);
    for (const auto& f : m_footprints) {
        if (upper(f->name) == want) return f.get();
        for (const std::string& n : f->cadNames)
            if (upper(n) == want) return f.get();
    }
    return nullptr;
}

std::vector<JPLibraryFootprint*> JPConfiguration::footprintsOf(const std::string& packageId) const {
    std::vector<JPLibraryFootprint*> out;
    for (const auto& f : m_footprints)
        if (upper(f->packageId) == upper(packageId)) out.push_back(f.get());
    return out;
}

JPLibraryFootprint* JPConfiguration::defaultFootprint(const std::string& packageId, bool make) {
    if (const auto of = footprintsOf(packageId); !of.empty()) return of.front();
    const JPPackage* k = libraryPackage(packageId);
    if (!make || !k) return nullptr;
    auto f = std::make_shared<JPLibraryFootprint>();
    f->name = k->id;
    f->packageId = k->id;
    f->geometry = k->footprint;
    f->source = "the package's own footprint";
    addFootprint(f);
    return m_footprints.back().get();
}

void JPConfiguration::addFootprint(std::shared_ptr<JPLibraryFootprint> f) {
    if (f->uuid.empty()) f->uuid = JPUuid::make();
    m_footprints.push_back(std::move(f));
}

void JPConfiguration::removeFootprint(const std::string& uuid) {
    std::erase_if(m_footprints, [&uuid](const auto& f) { return f->uuid == uuid; });
}

JPLibraryStore::Contents JPConfiguration::contents() const {
    JPLibraryStore::Contents c;
    c.parts = m_parts;
    c.packages = m_packages;
    c.footprints = m_footprints;
    c.manufacturers = m_manufacturers;
    return c;
}

void JPConfiguration::addPart(std::shared_ptr<JPPart> p) {
    if (p->uuid.empty()) p->uuid = JPUuid::make();
    const std::string k = upper(p->id);
    if (const auto it = m_partsById.find(k); it != m_partsById.end())
        *std::find(m_parts.begin(), m_parts.end(), it->second) = p;
    else m_parts.push_back(p);
    m_partsById[k] = std::move(p);
}

void JPConfiguration::removePart(const std::string& id) {
    const auto it = m_partsById.find(upper(id));
    if (it == m_partsById.end()) return;
    std::erase(m_parts, it->second);
    m_partsById.erase(it);
}

JPPackage* JPConfiguration::package(const std::string& id) const {
    const auto it = m_packagesById.find(upper(id));
    if (it != m_packagesById.end()) return it->second.get();
    // A board's own package, by its (board-scoped) id.
    const std::string k = upper(id);
    for (const auto& b : m_boards)
        for (const JPBoardPart& bp : b->parts()) {
            if (bp.localPackage && upper(bp.localPackage->id) == k) return bp.localPackage.get();
            if (bp.copyPackage && upper(bp.copyPackage->id) == k) return bp.copyPackage.get();
        }
    return nullptr;
}

void JPConfiguration::addPackage(std::shared_ptr<JPPackage> p) {
    if (p->uuid.empty()) p->uuid = JPUuid::make();
    const std::string k = upper(p->id);
    if (const auto it = m_packagesById.find(k); it != m_packagesById.end())
        *std::find(m_packages.begin(), m_packages.end(), it->second) = p;
    else m_packages.push_back(p);
    m_packagesById[k] = std::move(p);
}

void JPConfiguration::removePackage(const std::string& id) {
    const auto it = m_packagesById.find(upper(id));
    if (it == m_packagesById.end()) return;
    std::erase(m_packages, it->second);
    m_packagesById.erase(it);
}

std::shared_ptr<JPBoard> JPConfiguration::board(const std::string& path, std::string& error) {
    if (!exists(path)) {
        JPBoard b;
        b.name = fs::path(path).filename().string();
        if (JPBoard::isJplacerFile(path)) {
            if (!b.toJson().dumpToFile(path)) {
                error = "Unable to write " + path;
                return nullptr;
            }
        } else if (!JPXmlWriter::write(path, b.toXml(), error, false)) {
            return nullptr;
        }
    }
    std::string file = canonical(path);
    // OpenPnP's file that jplacer has saved as its own: that one (a job or panel naming the old file, saved
    // before it moved, or OpenPnP's, finds the board as it is now).
    if (!JPBoard::isJplacerFile(file)) {
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(file).parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
            const std::string candidate = it->path().string();
            if (!JPBoard::isJplacerFile(candidate)) continue;
            const std::optional<JJson> j = JJson::tryParseFile(candidate);
            if (j && (*j)["convertedFrom"].isString() && (*j)["convertedFrom"].str() == file) {
                file = canonical(candidate);
                break;
            }
        }
    }
    for (const auto& b : m_boards)
        if (b->file == file) return b;
    std::shared_ptr<JPBoard> b;
    if (JPBoard::isJplacerFile(file)) {
        const std::optional<JJson> j = JJson::tryParseFile(file);
        if (!j || !j->isObject() || !(*j)["format"].isString() || (*j)["format"].str() != JPBoard::kFormat) {
            error = "Not a jplacer board file: " + file;
            return nullptr;
        }
        b = std::make_shared<JPBoard>(JPBoard::fromJson(*j));
    } else {
        JPXmlElement root;
        if (!JPXmlReader::read(file, root, error)) return nullptr;
        b = std::make_shared<JPBoard>(JPBoard::fromXml(root));
        // OpenPnP's placements name library parts by id: each becomes a board part.
        b->partsFromPlacements([this](const std::string& id) { return libraryPart(id) != nullptr; });
        for (JPBoardPart& bp : b->parts())
            if (bp.state == JPBoardPart::State::Matched) takeCopy(bp);
    }
    b->file = file;
    b->dirty = false;
    m_boards.push_back(b);
    return b;
}

std::shared_ptr<JPPanel> JPConfiguration::panel(const std::string& path, std::string& error) {
    if (!exists(path)) {
        JPPanel p;
        p.name = fs::path(path).filename().string();
        if (!JPXmlWriter::write(path, p.toXml(), error, false)) return nullptr;
    }
    const std::string file = canonical(path);
    for (const auto& p : m_panels)
        if (p->file == file) return p;
    auto p = loadPanel(file, error);
    if (p) m_panels.push_back(p);
    return p;
}

std::shared_ptr<JPPanel> JPConfiguration::loadPanel(const std::string& file, std::string& error) {
    JPXmlElement root;
    if (!JPXmlReader::read(file, root, error)) return nullptr;
    auto p = std::make_shared<JPPanel>(JPPanel::fromXml(root));
    p->file = file;
    for (auto& c : p->children) {
        std::string childFile = c->fileName;
        if (!exists(childFile)) childFile = (fs::path(file).parent_path() / fs::path(childFile).filename()).string();
        if (!exists(childFile)) {
            error = "Unable to find child " + canonical(childFile) + " of panel " + file;
            return nullptr;
        }
        if (c->kind() == JPPlacementsHolderLocation::Kind::Board) {
            auto b = board(childFile, error);
            if (!b) return nullptr;
            c->holder = b->instance();
        } else {
            auto sub = panel(childFile, error);
            if (!sub) return nullptr;
            c->holder = sub->instance();
            static_cast<JPPanelLocation&>(*c).setParentsOfAllDescendants();
        }
    }
    p->dirty = false;
    return p;
}

void JPConfiguration::addBoard(std::shared_ptr<JPBoard> b) {
    for (auto& known : m_boards)
        if (known->file == b->file) {
            known = std::move(b);
            return;
        }
    m_boards.push_back(std::move(b));
}

void JPConfiguration::addPanel(std::shared_ptr<JPPanel> p) {
    for (auto& known : m_panels)
        if (known->file == p->file) {
            known = std::move(p);
            return;
        }
    m_panels.push_back(std::move(p));
}

void JPConfiguration::removeBoard(const JPBoard* b) {
    std::erase_if(m_boards, [b](const auto& p) { return p.get() == b; });
}

void JPConfiguration::removePanel(const JPPanel* p) {
    std::erase_if(m_panels, [p](const auto& q) { return q.get() == p; });
}

bool JPConfiguration::saveBoard(JPBoard& b, std::string& error, std::string* movedFrom) {
    // jplacer's file, always: an OpenPnP board is written beside it as one (its own name, ".jpboard"), the
    // OpenPnP file left as it is, and every panel naming it pointed at the new one.
    if (!JPBoard::isJplacerFile(b.file)) {
        const std::string from = b.file;
        std::string stem = fs::path(from).filename().string();
        for (const char* suffix : { ".board.xml", ".xml" })
            if (stem.size() > std::strlen(suffix)
                && upper(stem.substr(stem.size() - std::strlen(suffix))) == upper(suffix)) {
                stem.resize(stem.size() - std::strlen(suffix));
                break;
            }
        const fs::path dir = fs::path(from).parent_path();
        fs::path to = dir / (stem + JPBoard::kExtension);
        for (int n = 2; exists(to.string()); ++n) to = dir / (stem + " (" + std::to_string(n) + ")" + JPBoard::kExtension);
        b.file = to.string();
        b.convertedFrom = canonical(from);
        // Named after its file (as OpenPnP names a board): named after the new one.
        if (b.name && *b.name == fs::path(from).filename().string()) b.name = to.filename().string();
        if (movedFrom) *movedFrom = from;
        for (const auto& p : m_panels)
            for (auto& c : p->children)
                if (c->kind() == JPPlacementsHolderLocation::Kind::Board && canonical(c->fileName) == canonical(from)) {
                    c->fileName = b.file;
                    p->dirty = true;
                }
    }
    b.dropUnusedParts();   // a part no placement names any more is not kept
    // Every library part it uses carried in it (one matched without its copy yet: the library's now).
    for (JPBoardPart& bp : b.parts())
        if (bp.state == JPBoardPart::State::Matched && !bp.copyPart) takeCopy(bp);
    if (!b.toJson().dumpToFile(b.file)) {
        error = "Unable to write " + b.file;
        return false;
    }
    b.dirty = false;
    return true;
}

bool JPConfiguration::savePanel(JPPanel& p, std::string& error) const {
    if (!JPXmlWriter::write(p.file, p.toXml(), error, false)) return false;
    p.dirty = false;
    return true;
}

bool JPConfiguration::resolveBoard(JPJob* job, JPBoardLocation& l, std::string& error) {
    const std::string parentFile = l.parent && l.parent->holder ? l.parent->holder->file : std::string();
    const std::string file = find(l.fileName, parentFile, job ? job->file : std::string());
    if (file.empty()) {
        error = "Board file not found: " + l.fileName;
        return false;
    }
    auto def = board(file, error);
    if (!def) return false;
    // The revision the job was saved with, shown; the board is in the job at one revision only.
    if (!l.revision.empty() && l.revision != def->revisionLabel()) {
        const size_t i = def->revisionNamed(l.revision);
        if (i == def->revisions().size())
            JLOGC(JPlacerLog::kJob, JLogLevel::Warn)
                << "board " << file << ": the job was saved with its revision \"" << l.revision
                << "\", which it does not have; shown at \"" << def->revisionLabel() << "\"";
        else if (job && job->instanceCount(*def) > 0)
            JLOGC(JPlacerLog::kJob, JLogLevel::Warn)
                << "board " << file << ": in the job at \"" << def->revisionLabel() << "\" and at \"" << l.revision
                << "\"; shown at \"" << def->revisionLabel() << "\" for both";
        else
            def->showRevision(i);
    }
    if (!l.holder || l.holder->definition() != def.get()) l.holder = def->instance();
    return true;
}

bool JPConfiguration::resolvePanel(JPJob* job, JPPanelLocation& l, std::string& error) {
    if (job && &l == &job->root()) {
        l.panel()->file = job->file;
    } else {
        const std::string parentFile = l.parent && l.parent->holder ? l.parent->holder->file : std::string();
        const std::string file = find(l.fileName, parentFile, job ? job->file : std::string());
        if (file.empty()) {
            error = "Panel file not found: " + l.fileName;
            return false;
        }
        auto def = panel(file, error);
        if (!def) return false;
        if (!l.holder || l.holder->definition() != def.get()) {
            l.holder = def->instance();
            for (JPPlacementsHolderLocation* c : l.children()) c->parent = &l;
        }
    }
    for (JPPlacementsHolderLocation* c : l.children()) {
        c->parent = &l;
        const bool ok = c->kind() == JPPlacementsHolderLocation::Kind::Panel
                            ? resolvePanel(job, static_cast<JPPanelLocation&>(*c), error)
                            : resolveBoard(job, static_cast<JPBoardLocation&>(*c), error);
        if (!ok) return false;
    }
    l.panel()->dirty = false;
    return true;
}

std::vector<JPPlacementsHolderLocation*> JPConfiguration::instanceLocationsOf(const JPPlacementsHolder& def,
                                                                              const JPJob* job) const {
    std::vector<JPPlacementsHolderLocation*> out;
    auto take = [&](const std::vector<JPPlacementsHolderLocation*>& ls) {
        for (JPPlacementsHolderLocation* l : ls)
            if (l->holder && l->holder.get() != &def && l->holder->definition() == &def) out.push_back(l);
    };
    if (job) take(job->boardAndPanelLocations());
    for (const auto& p : m_panels) take(p->descendants());
    return out;
}

std::vector<JPPlacementsHolder*> JPConfiguration::instancesOf(const JPPlacementsHolder& def, const JPJob* job) const {
    std::vector<JPPlacementsHolder*> out;
    for (JPPlacementsHolderLocation* l : instanceLocationsOf(def, job)) out.push_back(l->holder.get());
    return out;
}

JPPlacementsHolder* JPConfiguration::definitionOf(const JPPlacementsHolder& h) const {
    const JPPlacementsHolder* def = h.definition();
    for (const auto& b : m_boards)
        if (b.get() == def) return b.get();
    for (const auto& p : m_panels)
        if (p.get() == def) return p.get();
    return nullptr;
}

bool JPConfiguration::isInUse(const JPPlacementsHolder& h, const JPJob* job) const {
    if (job && job->instanceCount(h) > 0) return true;
    for (const auto& p : m_panels)
        if (p->definition() != h.definition() && p->instanceCount(h) > 0) return true;
    return false;
}

void JPConfiguration::restoreJobSettings(JPJob& job, JPPanelLocation& l) {
    for (JPPlacementsHolderLocation* c : l.children()) {
        c->locallyEnabled = job.retrieveEnabledState(*c, nullptr);
        c->checkFiducials = job.retrieveCheckFiducialsState(*c);
        if (c->holder)
            for (JPPlacement& p : c->holder->placements) {
                p.enabled = job.retrieveEnabledState(*c, &p);
                p.errorHandling = job.retrieveErrorHandlingState(*c, p);
            }
        if (c->kind() == JPPlacementsHolderLocation::Kind::Panel) restoreJobSettings(job, static_cast<JPPanelLocation&>(*c));
    }
}

void JPConfiguration::saveJobSettings(JPJob& job, JPPanelLocation& l) {
    if (&l == &job.root()) {
        job.removeAllEnabledState();
        job.removeAllErrorHandlingState();
    }
    for (JPPlacementsHolderLocation* c : l.children()) {
        const JPPlacementsHolderLocation* d = c->definition();
        if (c->locallyEnabled != d->locallyEnabled) job.storeEnabledState(*c, nullptr, c->locallyEnabled);
        if (c->checkFiducials != d->checkFiducials) job.storeCheckFiducialsState(*c, c->checkFiducials);
        if (c->holder) {
            const JPPlacementsHolder* defHolder = c->holder->definition();
            for (const JPPlacement& p : c->holder->placements) {
                const JPPlacement* dp = defHolder->find(p.id);
                if (!dp) dp = &p;
                if (p.enabled != dp->enabled) job.storeEnabledState(*c, &p, p.enabled);
                if (p.errorHandling != dp->errorHandling) job.storeErrorHandlingState(*c, p, p.errorHandling);
            }
        }
        if (c->kind() == JPPlacementsHolderLocation::Kind::Panel) saveJobSettings(job, static_cast<JPPanelLocation&>(*c));
    }
}

bool JPConfiguration::convertLegacyJob(JPJob& job, std::string& error) {
    const bool panelised = !job.legacyPanels.empty();
    if (!panelised && job.legacyBoardLocations.empty()) return true;
    // The older file is kept beside it, as OpenPnP keeps it.
    const std::string jobFile = job.file;
    const std::string base = jobFile.size() > 8 && jobFile.ends_with(JPJob::kExtension)
                                 ? jobFile.substr(0, jobFile.size() - std::string(JPJob::kExtension).size())
                                 : jobFile;
    std::error_code ec;
    fs::copy_file(jobFile, base + ".legacy.job.xml", fs::copy_options::overwrite_existing, ec);
    const std::string delim = JPPlacementsHolderLocation::kIdDelimiter;

    if (panelised) {
        JPBoardLocation& rootBoard = *job.legacyBoardLocations.front();
        if (!resolveBoard(&job, rootBoard, error)) return false;
        auto panel = std::make_shared<JPPanel>(std::move(job.legacyPanels.front()));
        const JPPanel::Legacy legacy = panel->legacy.value_or(JPPanel::Legacy());
        panel->legacy.reset();
        panel->makeDefinition();
        panel->file = base + ".panel.xml";
        panel->name = fs::path(panel->file).filename().string();
        const JPLocation rootDims = rootBoard.board()->dimensions.convertToUnits(JPLengthUnit::Millimeters);
        panel->setDimensions(JPLocation::origin().deriveLengths(
            rootDims.lengthX().add(legacy.xGap).multiply(legacy.columns).subtract(legacy.xGap),
            rootDims.lengthY().add(legacy.yGap).multiply(legacy.rows).subtract(legacy.yGap), std::nullopt, std::nullopt));
        if (rootBoard.globalSide() == JPSide::Bottom)
            for (JPPlacement& f : panel->placements) {
                f.location = f.location.deriveLengths(panel->dimensions.lengthX().subtract(f.location.lengthX()),
                                                      std::nullopt, std::nullopt, std::nullopt);
                f.side = JPSide::Bottom;
            }
        const double stepX = rootDims.lengthX().add(legacy.xGap).value();
        const double stepY = rootDims.lengthY().add(legacy.yGap).value();
        for (int j = 0; j < legacy.rows; ++j)
            for (int i = 0; i < legacy.columns; ++i) {
                auto pcb = rootBoard.instance();
                pcb->makeDefinition();
                pcb->parent = nullptr;
                pcb->setGlobalSide(JPSide::Top);
                pcb->setLocation(JPLocation(JPLengthUnit::Millimeters, stepX * i, stepY * j, 0, 0));
                char id[48];
                std::snprintf(id, sizeof id, "%s[%d,%d]", JPBoardLocation::kIdPrefix, j + 1, i + 1);
                pcb->id = id;
                const size_t n = size_t(j * legacy.columns
                                        + (rootBoard.globalSide() == JPSide::Top ? i : legacy.columns - 1 - i));
                if (n < job.legacyBoardLocations.size()) {
                    const JPBoardLocation& sub = *job.legacyBoardLocations[n];
                    pcb->locallyEnabled = sub.locallyEnabled;
                    const std::string keyRoot = std::string(JPPanelLocation::kIdPrefix) + "1" + delim + pcb->id + delim;
                    for (const auto& [k, v] : sub.legacyPlaced) job.placedStatusMap[keyRoot + k] = v;
                }
                panel->addChild(std::move(pcb));
            }
        if (!savePanel(*panel, error)) return false;
        addPanel(panel);
        auto pl = std::make_unique<JPPanelLocation>();
        pl->fileName = panel->file;
        pl->setLocation(rootBoard.globalLocation());
        pl->side = rootBoard.globalSide();
        pl->checkFiducials = legacy.checkFids;
        job.addBoardOrPanelLocation(std::move(pl));
    } else {
        for (auto& bl : job.legacyBoardLocations) {
            const std::map<std::string, bool> placed = bl->legacyPlaced;
            JPPlacementsHolderLocation* added = job.root().addChild(std::move(bl));
            for (const auto& [id, v] : placed) job.storePlacedStatus(*added, id, v);
        }
    }
    job.legacyBoardLocations.clear();
    job.legacyPanels.clear();
    job.dirty = true;
    return true;
}

std::unique_ptr<JPJob> JPConfiguration::loadJob(const std::string& path, std::string& error) {
    JPXmlElement root;
    if (!JPXmlReader::read(path, root, error)) return nullptr;
    auto job = JPJob::fromXml(root);
    job->file = canonical(path);
    const bool older = !job->version;
    if (!convertLegacyJob(*job, error)) return nullptr;
    if (!resolvePanel(job.get(), job->root(), error)) return nullptr;
    restoreJobSettings(*job, job->root());
    job->dirty = older;
    return job;
}

bool JPConfiguration::saveJob(JPJob& job, const std::string& path, std::string& error) {
    saveJobSettings(job, job.root());
    if (!JPXmlWriter::write(path, job.toXml(), error, false)) return false;
    job.file = canonical(path);
    job.root().panel()->file = job.file;
    job.dirty = false;
    return true;
}

} // inline namespace jf
