// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPConfiguration.h"

#include "JPLibraryLearning.h"
#include "common/JPWhen.h"

#include "JPLibraryJson.h"
#include "JPRunLedger.h"

#include "common/JPUuid.h"
#include "common/JPWholeFile.h"
#include "common/JPlacerLog.h"

#include "openpnp/JPXmlJson.h"
#include "openpnp/JPXmlReader.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <tuple>
#include <utility>

inline namespace jf {

namespace fs = std::filesystem;

namespace {

// What each kind is known by for good: a part by its id, a package and a footprint by their uuid.
const std::string& keyOf(const JPPart& p) { return p.id; }
const std::string& keyOf(const JPPackage& k) { return k.id; }
const std::string& keyOf(const JPLibraryFootprint& f) { return f.uuid; }


// The library's vision links taken from OpenPnP's files (a library without it has them taken once).

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

// An OpenPnP file's name without its kind's suffix ("main.board.xml": "main").
std::string openPnpStem(const std::string& file) {
    std::string stem = fs::path(file).filename().string();
    for (const char* suffix : { ".board.xml", ".panel.xml", ".job.xml", ".xml" })
        if (stem.size() > std::strlen(suffix) && upper(stem.substr(stem.size() - std::strlen(suffix))) == upper(suffix)) {
            stem.resize(stem.size() - std::strlen(suffix));
            break;
        }
    return stem;
}

// jplacer's file for OpenPnP's `from`: beside it, its name with `extension` (numbered when that is taken).
std::string besideAs(const std::string& from, const char* extension) {
    const std::string stem = openPnpStem(from);
    const fs::path dir = fs::path(from).parent_path();
    fs::path to = dir / (stem + extension);
    for (int n = 2; exists(to.string()); ++n) to = dir / (stem + " (" + std::to_string(n) + ")" + extension);
    return to.string();
}

// jplacer's file beside OpenPnP's `file` that was first saved from it (its "convertedFrom"), else `file`.
std::string convertedOf(const std::string& file, bool (*own)(const std::string&)) {
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(file).parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string candidate = it->path().string();
        if (!own(candidate)) continue;
        const std::optional<JJson> j = JJson::tryParseFile(candidate);
        if (j && (*j)["convertedFrom"].str() == file) return JPConfiguration::canonical(candidate);
    }
    return file;
}

// One of jplacer's files (a board, panel or job): JSON of `format`.
std::optional<JJson> readOwn(const std::string& file, const char* format, const char* what, std::string& error) {
    std::optional<JJson> j = JJson::tryParseFile(file);
    if (!j || !j->isObject() || (*j)["format"].str() != format) {
        error = std::string("Not a jplacer ") + what + " file: " + file;
        return std::nullopt;
    }
    return j;
}

constexpr const char* kFeedersFormat = "jplacer-feeders";
constexpr const char* kPropertiesFormat = "jplacer-machine-properties";
constexpr const char* kVisionFormat = "jplacer-vision-settings";

// One of jplacer's JSON files of the configuration: missing, an empty object; else it must be of `format`.
bool readJson(const fs::path& p, const char* format, JJson& out, std::string& error) {
    out = JJson::object();
    if (!exists(p.string())) return true;
    std::optional<JJson> j = JJson::tryParseFile(p.string());
    if (!j || !j->isObject() || (*j)["format"].str() != format) {
        error = "Not a jplacer " + std::string(format + std::strlen("jplacer-")) + " file: " + p.string();
        return false;
    }
    out = std::move(*j);
    return true;
}

bool writeJson(const fs::path& p, const char* format, const char* key, JJson value, std::string& error) {
    JJson j = JJson::object();
    j["format"] = format;
    j["version"] = 1;
    j[key] = std::move(value);
    return JPWholeFile::write(p.string(), j, error);
}

// Machine properties (OpenPnP's <entry><string>name</string><object …/></entry>) as {name: object} and back.
JJson propertiesJson(const std::vector<JPXmlNode>& entries) {
    JJson out = JJson::object();
    for (const JPXmlNode& e : entries) {
        const JPXmlNode* name = e.child("string");
        const JPXmlNode* object = e.child("object");
        if (name && object) out[name->text] = JPXmlJson::from(*object);
    }
    return out;
}

std::vector<JPXmlElement> propertyEntries(const JJson& properties) {
    std::vector<JPXmlElement> out;
    for (const auto& [name, object] : properties.obj()) {
        JPXmlElement e;
        e.name = "entry";
        JPXmlElement key;
        key.name = "string";
        key.text = name;
        e.children.push_back(std::move(key));
        e.children.push_back(JPXmlJson::element("object", object));
        out.push_back(std::move(e));
    }
    return out;
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
    // Not there yet: OpenPnP's default, when there is one (`fromDefaults` says so).
    auto orDefault = [this, &dir](const char* file, bool& fromDefaults) {
        if (m_defaults.empty() || exists((dir / file).string()) || !exists((fs::path(m_defaults) / file).string()))
            return dir / file;
        fromDefaults = true;
        return fs::path(m_defaults) / file;
    };
    if (!m_fields.load((dir / JPFields::kFile).string(), error)) return false;
    bool libraryDefaults = false, visionDefaults = false;
    JPXmlElement packages, parts, openPnpVision;
    JJson feeders, properties, vision;
    const bool hadVision = exists((dir / kVisionFile).string());
    if (!read(orDefault(kPackagesFile, libraryDefaults), packages) || !read(orDefault(kPartsFile, libraryDefaults), parts)
        || (!hadVision && !read(orDefault(kOpenPnpVisionFile, visionDefaults), openPnpVision))
        || !readJson(dir / kFeedersFile, kFeedersFormat, feeders, error)
        || !readJson(dir / kMachinePropertiesFile, kPropertiesFormat, properties, error)
        || !readJson(dir / kVisionFile, kVisionFormat, vision, error))
        return false;
    for (const JJson& f : std::as_const(feeders)["feeders"].arr()) m_feeders.push_back(JPFeeder::fromJson(f));
    for (const JPXmlElement& e : propertyEntries(std::as_const(properties)["properties"])) {
        if (auto b = JPSlotBanks::fromXml(e)) m_slotBanks.push_back(std::move(*b));
        else if (auto d = JPDropBoxes::fromXml(e)) m_dropBoxes = std::move(*d);
        else m_photon.take(e);
    }
    resolveSlots();
    resolvePhoton();
    for (const JJson& v : std::as_const(vision)["settings"].arr()) {
        // Its values of the fields added beside what OpenPnP keeps.
        JJson own = v;
        own.erase("custom");
        m_vision.push_back(JPVisionSettings::fromXml(JPXmlJson::element("vision-settings", own)));
        m_vision.back().custom = JPFieldValues::read(v);
    }
    for (const JPXmlElement& e : openPnpVision.children)
        if (e.name == "vision-settings") m_vision.push_back(JPVisionSettings::fromXml(e));
    m_tookDefaults = visionDefaults;
    // The library: library.db, else made from OpenPnP's parts.xml and packages.xml (left as they are); then any of
    // those files' parts and packages it lacks, when the files changed since it last looked (OpenPnP's copied in).
    const fs::path libraryFile = dir / JPLibraryStore::kFile;
    const bool hadLibrary = exists(libraryFile.string());
    if (!m_library.open(libraryFile.string(), error) || !m_stock.open(libraryFile.string(), error)
        || !m_runs.open((dir / JPRunStore::kFile).string(), error))
        return false;
    // Feeders known by OpenPnP's ids (an older file's): by GUIDs now, kept so.
    if (feederIds() && !writeJson(dir / kFeedersFile, kFeedersFormat, "feeders", feedersJson(), error)) return false;
    // Runs whose parts are not in the ledger yet (jplacer closed or stopped while one ran): written now.
    {
        const size_t open = m_runs.unledgered().size();
        std::string why;
        const int written = JPRunLedger::writeAll(m_runs, m_stock, why);
        if (written > 0)
            JLOGC(JPlacerLog::kJob, JLogLevel::Info) << written << " run(s) not yet in the stock's ledger written to it";
        if (size_t(written) < open)
            problems.push_back("The parts of " + std::to_string(open - written) + " run(s) could not be written to the stock's "
                               "ledger: " + why);
    }
    if (hadLibrary) {
        JPLibraryStore::Contents in;
        if (!m_library.load(in, error)) return false;
        for (auto& k : in.packages) addPackage(std::move(k));
        for (auto& p : in.parts) addPart(std::move(p));
        for (auto& f : in.footprints) addFootprint(std::move(f));
        m_manufacturers = std::move(in.manufacturers);
    }
    const std::string stamp = openPnpStamp();
    if (!hadLibrary || stamp != m_library.meta("openpnpFiles")) {
        int added = 0, linked = 0;
        // A package or part the library has with no vision settings of its own takes OpenPnP's (its own never
        // replaced): a package made by a board's import before OpenPnP's files were read had none, and its
        // fiducials were found by the machine's default settings, not their own.
        auto link = [&linked](std::string& own, const std::string& openpnp) {
            if (!own.empty() || openpnp.empty()) return;
            own = openpnp;
            ++linked;
        };
        for (const JPXmlElement& e : packages.children) {
            if (e.name != "package") continue;
            // OpenPnP's id is the name: the library's package of that name, else one made (a GUID its id).
            if (JPPackage* k = packageNamed(e.attr("id"))) {
                link(k->bottomVisionId, e.attr("bottom-vision-id"));
                link(k->fiducialVisionId, e.attr("fiducial-vision-id"));
                continue;
            }
            auto made = std::make_shared<JPPackage>(JPPackage::fromXml(e));
            made->label = made->id;
            made->id = JPUuid::make();
            addPackage(std::move(made));
            ++added;
        }
        for (const JPXmlElement& e : parts.children) {
            if (e.name != "part") continue;
            // OpenPnP's id is the name: the library's part of that name, else one made (a GUID its id).
            if (JPPart* p = partNamed(e.attr("id"))) {
                link(p->bottomVisionId, e.attr("bottom-vision-id"));
                link(p->fiducialVisionId, e.attr("fiducial-vision-id"));
                continue;
            }
            auto made = std::make_shared<JPPart>(JPPart::fromXml(e));
            made->label = made->id;
            made->id = JPUuid::make();
            // Its package by OpenPnP's id for it, the library's package's name.
            if (const JPPackage* k = packageNamed(made->packageId)) made->packageId = k->id;
            addPart(std::move(made));
            ++added;
        }
        // OpenPnP's defaults taken only when the library took something of them.
        if (libraryDefaults && (added > 0 || linked > 0)) m_tookDefaults = true;
        if (hadLibrary && linked > 0)
            problems.push_back(std::to_string(linked) + " vision settings link(s) of OpenPnP's parts.xml and packages.xml were given "
                               "to the library's parts and packages that had none");
        if (hadLibrary && added > 0)
            problems.push_back(std::to_string(added) + " part(s) and package(s) of OpenPnP's parts.xml and packages.xml "
                               "the library did not have were added to it");
        std::string why;
        if (!m_library.save(contents(), why) || !m_library.setMeta("openpnpFiles", stamp)) {
            error = why;
            return false;
        }
    }
    return true;
}

bool JPConfiguration::save(std::string& error) const {
    const fs::path dir(m_directory);
    // The library's parts and packages, to library.db (OpenPnP's parts.xml and packages.xml are not written).
    if (!m_library.isOpen() && !m_library.open((dir / JPLibraryStore::kFile).string(), error)) return false;
    if (!m_library.save(contents(), error)) return false;
    const JJson feeders = feedersJson();
    std::vector<JPXmlNode> entries;
    for (const JPSlotBanks& b : m_slotBanks) entries.push_back(b.toXml());
    for (JPXmlNode& e : m_photon.toXml()) entries.push_back(std::move(e));
    entries.push_back(m_dropBoxes.toXml());
    JJson vs = JJson::array();
    for (const JPVisionSettings& v : m_vision) {
        JJson j = JPXmlJson::from(v.toXml());
        JPFieldValues::write(j, v.custom);
        vs.push(j);
    }
    return saveFields(error) && writeJson(dir / kFeedersFile, kFeedersFormat, "feeders", feeders, error)
           && writeJson(dir / kMachinePropertiesFile, kPropertiesFormat, "properties", propertiesJson(entries), error)
           && writeJson(dir / kVisionFile, kVisionFormat, "settings", vs, error);
}

bool JPConfiguration::saveFields(std::string& error) const {
    return m_fields.save((fs::path(m_directory) / JPFields::kFile).string(), error);
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
    // OpenPnP names a part by its id, which is the part's Name here: each named so linked to that part's id.
    auto linked = [this](const std::string& name) {
        const JPPart* p = name.empty() ? nullptr : partNamed(name);
        return p ? p->id : name;
    };
    for (JPFeeder& f : m_feeders)
        if (!f.isSlot() && !f.partId().empty()) f.setPartId(linked(f.partId()));
    for (JPSlotBanks& banks : m_slotBanks)
        for (const JPSlotBanks::Bank& b : banks.banks())
            for (const JPSlotBanks::Feeder& bf : banks.feeders(b.id))
                if (!bf.partId.empty()) banks.setFeederPart(b.id, bf.id, linked(bf.partId));
    for (const JPDropBoxes::Box& b : m_dropBoxes.boxes())
        if (!b.dummyPartId.empty()) m_dropBoxes.setDummyPart(b.id, linked(b.dummyPartId));
    resolveSlots();
    resolvePhoton();
    feederIds();
    return int(m_feeders.size());
}

JJson JPConfiguration::feedersJson() const {
    JJson feeders = JJson::array();
    for (const JPFeeder& f : m_feeders) feeders.push(f.toJson());
    return feeders;
}

bool JPConfiguration::feederIds() {
    bool any = false;
    for (JPFeeder& f : m_feeders) {
        if (JPUuid::is(f.id())) continue;
        // OpenPnP's id ("FDR…"), or one of an older jplacer's: a GUID for good, any lot on it following it.
        const std::string was = f.id();
        f.setId(JPUuid::make());
        m_stock.renameFeeder(was, f.id());
        any = true;
    }
    return any;
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
    const std::string own = slot.slotBankId();
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
        const std::string bank = slotBankId(f), feederId = f.slotFeederId();
        const auto feeder = feederId.empty() ? std::nullopt : slotBanks(f.typeName()).feeder(bank, feederId);
        if (!feeder) continue;
        auto& holder = loaded[{ f.typeName(), bank, feederId }];
        if (holder) {
            holder->slotLoad.reset();
            holder->setSlotFeederId("");
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
        if (f.isPhoton() && f.hardwareId() == hardwareId) return &f;
    return nullptr;
}

void JPConfiguration::loadSlot(const std::string& slotId, const std::string& bankFeederId) {
    JPFeeder* slot = feeder(slotId);
    if (!slot) return;
    const std::string bank = slotBankId(*slot);
    if (!bankFeederId.empty())
        for (JPFeeder& other : m_feeders)
            if (&other != slot && other.typeName() == slot->typeName() && other.slotFeederId() == bankFeederId
                && slotBankId(other) == bank)
                other.setSlotFeederId("");
    slot->setSlotBankId(bank);
    slot->setSlotFeederId(bankFeederId);
    resolveSlots();
}

void JPConfiguration::setSlotBank(const std::string& slotId, const std::string& bankId) {
    JPFeeder* slot = feeder(slotId);
    if (!slot) return;
    slot->setSlotBankId(bankId);
    slot->setSlotFeederId("");
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
        if ((bottom ? p->bottomVisionId : p->fiducialVisionId) == v.id) packages.push_back(p->shownName());
    for (const auto& p : m_parts)
        if ((bottom ? p->bottomVisionId : p->fiducialVisionId) == v.id) parts.push_back(p->shownName());
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
    return libraryPart(id);
}

JPPart* JPConfiguration::libraryPart(const std::string& id) const {
    const auto it = m_partsById.find(upper(id));
    return it == m_partsById.end() ? nullptr : it->second.get();
}

JPPart* JPConfiguration::partNamed(const std::string& name) const {
    const std::string wanted = upper(name);
    for (const auto& p : m_parts)
        if (upper(p->shownName()) == wanted) return p.get();
    return nullptr;
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

JPPart* JPConfiguration::libraryPartFor(const JPBoardPart& bp) const { return libraryPart(bp.libraryPartId); }

const JPLibraryFootprint* JPConfiguration::footprintFor(const JPBoardPart& bp, const JPPart& p) const {
    // The one its CAD footprint names, when it is of the part's package; else the package's first.
    const std::string cad = !bp.field("footprint").empty() ? bp.field("footprint") : bp.field("package");
    if (const JPLibraryFootprint* f = footprintNamed(cad); f && upper(f->packageId) == upper(p.packageId)) return f;
    const auto of = footprintsOf(p.packageId);
    return of.empty() ? nullptr : of.front();
}

int JPConfiguration::takeMade(const JPBoard& imported, int& packagesAdded) {
    packagesAdded = 0;
    for (const auto& k : imported.madePackages)
        if (!libraryPackage(k->id)) {
            auto package = std::make_shared<JPPackage>(*k);
            // Names stay unique: one the library has by now numbered.
            if (!package->label.empty() && packageNamed(package->label)) {
                const std::string base = package->label;
                for (int n = 2; packageNamed(package->label); ++n) package->label = base + " (" + std::to_string(n) + ")";
            }
            addPackage(std::move(package));
            ++packagesAdded;
        }
    int parts = 0;
    for (const auto& p : imported.madeParts)
        if (!libraryPart(p->id)) {
            auto part = std::make_shared<JPPart>(*p);
            // Names stay unique: one the library has by now numbered.
            if (!part->label.empty() && (partNamed(part->label) || libraryPart(part->label))) {
                const std::string base = part->label;
                for (int n = 2; partNamed(part->label) || libraryPart(part->label); ++n) part->label = base + " (" + std::to_string(n) + ")";
            }
            addPart(std::move(part));
            ++parts;
        }
    // What the board calls each part it is linked to, known by the library's part from now on (its AKA); and what
    // the files said of the fields added to parts and packages, where the part (or its package) has no value yet.
    for (const JPBoardPart& bp : imported.parts())
        if (bp.state == JPBoardPart::State::Matched)
            if (JPPart* part = libraryPartFor(bp)) {
                JPLibraryLearning::learnName(*part, bp, imported.scopeName(), JPWhen::now());
                const JPFieldValues partValues = bp.added(m_fields, JPFieldSet::Of::Parts);
                for (const auto& [key, v] : partValues.all())
                    if (!part->custom.has(key)) part->custom.set(key, v);
                const JPFieldValues packageValues = bp.added(m_fields, JPFieldSet::Of::Packages);
                if (JPPackage* k = package(part->packageId))
                    for (const auto& [key, v] : packageValues.all())
                        if (!k->custom.has(key)) k->custom.set(key, v);
            }
    return parts;
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

JPPackage* JPConfiguration::packageNamed(const std::string& name) const {
    if (name.empty()) return nullptr;
    // By its Name, else by a name its footprints are known by (what CAD files have called it).
    const std::string wanted = upper(name);
    for (const auto& k : m_packages)
        if (upper(k->label) == wanted) return k.get();
    const JPLibraryFootprint* f = footprintNamed(name);
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
    f->name = k->label;
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

JPLibraryStore::Contents JPConfiguration::librarySnapshot() const {
    JPLibraryStore::Contents c;
    for (const auto& p : m_parts) c.parts.push_back(std::make_shared<JPPart>(*p));
    for (const auto& k : m_packages) c.packages.push_back(std::make_shared<JPPackage>(*k));
    for (const auto& f : m_footprints) c.footprints.push_back(std::make_shared<JPLibraryFootprint>(*f));
    c.manufacturers = m_manufacturers;
    return c;
}

void JPConfiguration::restoreLibrary(const JPLibraryStore::Contents& snapshot) {
    // Each of `from`, made like the snapshot's of what it is known by for good (kept, changed in place) or a copy.
    auto rebuild = [](auto& list, const auto& from) {
        using Ptr = typename std::decay_t<decltype(list)>::value_type;
        std::map<std::string, Ptr> byKey;
        for (const auto& x : list) byKey[keyOf(*x)] = x;
        std::decay_t<decltype(list)> out;
        for (const auto& x : from) {
            const auto it = byKey.find(keyOf(*x));
            if (it != byKey.end()) {
                *it->second = *x;
                out.push_back(it->second);
            } else {
                out.push_back(std::make_shared<typename Ptr::element_type>(*x));
            }
        }
        list = std::move(out);
    };
    rebuild(m_parts, snapshot.parts);
    rebuild(m_packages, snapshot.packages);
    rebuild(m_footprints, snapshot.footprints);
    m_manufacturers = snapshot.manufacturers;
    m_partsById.clear();
    for (const auto& p : m_parts) m_partsById[upper(p->id)] = p;
    m_packagesById.clear();
    for (const auto& k : m_packages) m_packagesById[upper(k->id)] = k;
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
    // Known for good by a GUID, and named (its id, until it is given another name, when it comes with none).
    if (p->id.empty()) p->id = JPUuid::make();
    if (p->label.empty()) p->label = p->id;
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
    return libraryPackage(id);
}

void JPConfiguration::addPackage(std::shared_ptr<JPPackage> p) {
    // Known for good by a GUID, and named (its id, until it is given another name, when it comes with none).
    if (p->id.empty()) p->id = JPUuid::make();
    if (p->label.empty()) p->label = p->id;
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
        if (!JPBoard::isJplacerFile(path)) {
            error = "Board file not found: " + path;
            return nullptr;
        }
        JPBoard b;
        b.name = fs::path(path).filename().string();
        if (!JPWholeFile::write(path, b.toJson(), error)) return nullptr;
    }
    // OpenPnP's file that jplacer has saved as its own: that one (a job or panel naming the old file, saved
    // before it moved, or OpenPnP's, finds the board as it is now).
    std::string file = canonical(path);
    if (!JPBoard::isJplacerFile(file)) file = convertedOf(file, &JPBoard::isJplacerFile);
    for (const auto& b : m_boards)
        if (b->file == file) return b;
    std::shared_ptr<JPBoard> b;
    if (JPBoard::isJplacerFile(file)) {
        const std::optional<JJson> j = readOwn(file, JPBoard::kFormat, "board", error);
        if (!j) return nullptr;
        b = std::make_shared<JPBoard>(JPBoard::fromJson(*j));
    } else {
        JPXmlElement root;
        if (!JPXmlReader::read(file, root, error)) return nullptr;
        b = std::make_shared<JPBoard>(JPBoard::fromXml(root));
        // OpenPnP's placements name parts by OpenPnP's id: each becomes a board part, linked to the library's
        // part of that name (or id).
        b->partsFromPlacements([this](const std::string& name) {
            const JPPart* p = libraryPart(name);
            if (!p) p = partNamed(name);
            return p ? p->id : std::string();
        });
    }
    b->file = file;
    b->dirty = false;
    m_boards.push_back(b);
    return b;
}

std::shared_ptr<JPPanel> JPConfiguration::panel(const std::string& path, std::string& error) {
    if (!exists(path)) {
        if (!JPPanel::isJplacerFile(path)) {
            error = "Panel file not found: " + path;
            return nullptr;
        }
        JPPanel p;
        p.name = fs::path(path).filename().string();
        if (!JPWholeFile::write(path, p.toJson(), error)) return nullptr;
    }
    std::string file = canonical(path);
    if (!JPPanel::isJplacerFile(file)) file = convertedOf(file, &JPPanel::isJplacerFile);
    for (const auto& p : m_panels)
        if (p->file == file) return p;
    auto p = loadPanel(file, error);
    if (p) m_panels.push_back(p);
    return p;
}

void JPConfiguration::linkPartsByName(std::vector<JPPlacement>& placements) const {
    for (JPPlacement& p : placements)
        if (!p.partId.empty() && !libraryPart(p.partId))
            if (const JPPart* named = partNamed(p.partId)) p.partId = named->id;
}

std::shared_ptr<JPPanel> JPConfiguration::loadPanel(const std::string& file, std::string& error) {
    std::shared_ptr<JPPanel> p;
    if (JPPanel::isJplacerFile(file)) {
        const std::optional<JJson> j = readOwn(file, JPPanel::kFormat, "panel", error);
        if (!j) return nullptr;
        p = std::make_shared<JPPanel>(JPPanel::fromJson(*j));
    } else {
        JPXmlElement root;
        if (!JPXmlReader::read(file, root, error)) return nullptr;
        p = std::make_shared<JPPanel>(JPPanel::fromXml(root));
        linkPartsByName(p->placements);
    }
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

JPConfiguration::Open JPConfiguration::closeAll() {
    Open closed { std::move(m_boards), std::move(m_panels) };
    m_boards.clear();
    m_panels.clear();
    return closed;
}

void JPConfiguration::reopen(Open open) {
    m_boards = std::move(open.boards);
    m_panels = std::move(open.panels);
}

void JPConfiguration::removeBoard(const JPBoard* b) {
    std::erase_if(m_boards, [b](const auto& p) { return p.get() == b; });
}

void JPConfiguration::removePanel(const JPPanel* p) {
    std::erase_if(m_panels, [p](const auto& q) { return q.get() == p; });
}

void JPConfiguration::repoint(JPPlacementsHolderLocation::Kind kind, const std::string& from, const std::string& to) {
    for (const auto& p : m_panels)
        for (auto& c : p->children)
            if (c->kind() == kind && canonical(c->fileName) == canonical(from)) {
                c->fileName = to;
                p->dirty = true;
            }
}

bool JPConfiguration::saveBoard(JPBoard& b, std::string& error, std::string* movedFrom) {
    // jplacer's file, always: an OpenPnP board is written beside it as one (its own name, ".jpboard"), the
    // OpenPnP file left as it is, and every panel naming it pointed at the new one.
    if (!JPBoard::isJplacerFile(b.file)) {
        const std::string from = b.file;
        b.file = besideAs(from, JPBoard::kExtension);
        b.convertedFrom = canonical(from);
        // Named after its file (as OpenPnP names a board): named after the new one.
        if (b.name && *b.name == fs::path(from).filename().string()) b.name = fs::path(b.file).filename().string();
        if (movedFrom) *movedFrom = from;
        repoint(JPPlacementsHolderLocation::Kind::Board, from, b.file);
    }
    b.dropUnusedParts();   // a part no placement names any more is not kept
    if (!JPWholeFile::write(b.file, b.toJson(), error)) return false;
    b.dirty = false;
    return true;
}

bool JPConfiguration::savePanel(JPPanel& p, std::string& error, std::string* movedFrom) {
    // As a board: jplacer's file, an OpenPnP panel written beside it as one.
    if (!JPPanel::isJplacerFile(p.file)) {
        const std::string from = p.file;
        p.file = besideAs(from, JPPanel::kExtension);
        p.convertedFrom = canonical(from);
        if (p.name && *p.name == fs::path(from).filename().string()) p.name = fs::path(p.file).filename().string();
        if (movedFrom) *movedFrom = from;
        repoint(JPPlacementsHolderLocation::Kind::Panel, from, p.file);
    }
    if (!JPWholeFile::write(p.file, p.toJson(), error)) return false;
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
    const std::string delim = JPPlacementsHolderLocation::kIdDelimiter;

    if (panelised) {
        JPBoardLocation& rootBoard = *job.legacyBoardLocations.front();
        if (!resolveBoard(&job, rootBoard, error)) return false;
        auto panel = std::make_shared<JPPanel>(std::move(job.legacyPanels.front()));
        const JPPanel::Legacy legacy = panel->legacy.value_or(JPPanel::Legacy());
        panel->legacy.reset();
        panel->makeDefinition();
        panel->file = besideAs(job.file, JPPanel::kExtension);
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
    std::unique_ptr<JPJob> job;
    bool older = false;
    if (JPJob::isJplacerFile(path)) {
        const std::optional<JJson> j = readOwn(path, JPJob::kFormat, "job", error);
        if (!j) return nullptr;
        job = JPJob::fromJson(*j);
        job->file = canonical(path);
    } else {
        // OpenPnP's: its parts named by OpenPnP's ids (the library's Names); an older one's boards and panel made
        // its root's children.
        JPXmlElement root;
        if (!JPXmlReader::read(path, root, error)) return nullptr;
        job = JPJob::fromXml(root);
        job->file = canonical(path);
        older = !job->version;
        if (!convertLegacyJob(*job, error)) return nullptr;
        if (JPPanel* own = job->root().panel()) linkPartsByName(own->placements);
    }
    if (!resolvePanel(job.get(), job->root(), error)) return nullptr;
    restoreJobSettings(*job, job->root());
    job->dirty = older;
    return job;
}

bool JPConfiguration::saveJob(JPJob& job, const std::string& path, std::string& error) {
    // jplacer's file, always: an OpenPnP job is written beside it as one, the OpenPnP file left as it is.
    const std::string file = JPJob::isJplacerFile(path) ? path : besideAs(path, JPJob::kExtension);
    saveJobSettings(job, job.root());
    if (!JPWholeFile::write(file, job.toJson(), error)) return false;
    job.file = canonical(file);
    job.root().panel()->file = job.file;
    job.dirty = false;
    return true;
}

} // inline namespace jf
