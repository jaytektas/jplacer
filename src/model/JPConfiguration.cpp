// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPConfiguration.h"

#include "JPXmlValues.h"

#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

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
    JPXmlElement packages, parts, boards, panels;
    if (!read(dir / kPackagesFile, packages) || !read(dir / kPartsFile, parts) || !read(dir / kBoardsFile, boards)
        || !read(dir / kPanelsFile, panels))
        return false;
    for (const JPXmlElement& e : packages.children)
        if (e.name == "package") addPackage(std::make_shared<JPPackage>(JPPackage::fromXml(e)));
    for (const JPXmlElement& e : parts.children)
        if (e.name == "part") addPart(std::make_shared<JPPart>(JPPart::fromXml(e)));
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
    JPXmlNode parts("openpnp-parts");
    for (const auto& p : m_parts) parts.add(p->toXml());
    JPXmlNode packages("openpnp-packages");
    for (const auto& p : m_packages) packages.add(p->toXml());
    JPXmlNode boards("openpnp-boards");
    for (const auto& b : m_boards) boards.add(JPXmlNode("board")).text = b->file;
    JPXmlNode panels("openpnp-panels");
    for (const auto& p : m_panels) panels.add(JPXmlNode("panel")).text = p->file;
    return JPXmlWriter::write((dir / kPackagesFile).string(), packages, error)
        && JPXmlWriter::write((dir / kPartsFile).string(), parts, error)
        && JPXmlWriter::write((dir / kBoardsFile).string(), boards, error)
        && JPXmlWriter::write((dir / kPanelsFile).string(), panels, error);
}

JPPart* JPConfiguration::part(const std::string& id) const {
    const auto it = m_partsById.find(upper(id));
    return it == m_partsById.end() ? nullptr : it->second.get();
}

void JPConfiguration::addPart(std::shared_ptr<JPPart> p) {
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
    return it == m_packagesById.end() ? nullptr : it->second.get();
}

void JPConfiguration::addPackage(std::shared_ptr<JPPackage> p) {
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
        if (!JPXmlWriter::write(path, b.toXml(), error, false)) return nullptr;
    }
    const std::string file = canonical(path);
    for (const auto& b : m_boards)
        if (b->file == file) return b;
    JPXmlElement root;
    if (!JPXmlReader::read(file, root, error)) return nullptr;
    auto b = std::make_shared<JPBoard>(JPBoard::fromXml(root));
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

bool JPConfiguration::saveBoard(JPBoard& b, std::string& error) const {
    if (!JPXmlWriter::write(b.file, b.toXml(), error, false)) return false;
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
