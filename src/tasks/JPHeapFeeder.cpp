// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPHeapFeeder.h"

#include "JPFeederPipelines.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

inline namespace jf {

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
// OpenPnP's: the looks for a good part before fetching more, the parts a box is cleaned of at most, the looks at a part.
constexpr int kMaxThrowRetries = 12, kMaxCleanAttempts = 30, kPartLooks = 3;
// A part found farther than this from the box's centre is outside it (mm).
constexpr double kOutsideBoxMm = 8;
// How long each look and the last are shown.
constexpr int kLookShownMs = 250, kLastShownMs = 500;
// A stable vacuum difference: this many reads, this far apart, all above it.
constexpr int kStableReads = 5, kStableReadMs = 5;
// The vacuum is let settle 1.3 times the pick dwell before its level is taken, over three reads.
constexpr double kSettleFactor = 1.3;
constexpr int    kBaselineReads = 3;
// Stirring: the corners of a square 2.25 mm across, in OpenPnP's order, at a quarter speed, a step at most 0.1 mm down.
constexpr double kCornerMm = 1.125, kStirSpeed = 0.25, kStirStepMm = 0.1;
constexpr int    kCorners[] = { 0, 1, 3, 2, 0, 3, 1, 2 };
// Poking: a 5 x 5 grid 0.625 mm apart, down at a third of the speed, up again at full; a step at least 0.05 mm down.
constexpr double kPokeOriginMm = -1.25, kPokeGridMm = 0.625, kPokeSpeed = 0.33, kPokeStepMinMm = 0.05, kPokeLiftMinMm = 1,
                 kPokeLiftFactor = 2.25;
constexpr int kPokeGrid = 5, kPokeCycle = 25;

double mm(const JPLength& l) { return l.convertToUnits(kMm).value(); }

// One heap feeder's work with one nozzle.
class Heap {
public:
    Heap(JPConfiguration& config, std::string feederId, std::string nozzleId, JPJobMachine& machine, const JPHeapFeeder::OnMain& onMain)
        : m_config(config), m_feederId(std::move(feederId)), m_nozzleId(std::move(nozzleId)), m_machine(machine), m_onMain(onMain) {}

    void main(const std::function<void()>& fn) const {
        if (m_onMain) m_onMain(fn);
        else fn();
    }

    struct Feeder {
        std::string id, name, partId, boxId;
        JPLocation  location { kMm }, way1 { kMm }, way2 { kMm }, way3 { kMm };
        double      boxDepth = -25, lastFeedDepth = 0, partHeight = 0;
        int         throwAway = 9, vacuumDifference = 150;
        bool        poke = false;
    };
    // A heap feeder as it is now (none: no such feeder).
    std::optional<Feeder> feeder(const std::string& id) const {
        std::optional<Feeder> f;
        main([&] {
            const JPFeeder* jf = m_config.feeder(id);
            if (!jf) return;
            Feeder out;
            out.id = id;
            out.name = jf->name();
            out.partId = jf->partId();
            out.boxId = JPFeederPipelines::dropBoxOf(*jf, m_config.dropBoxes());
            out.location = jf->location().convertToUnits(kMm);
            out.way1 = jf->locationOf("way-1").convertToUnits(kMm);
            out.way2 = jf->locationOf("way-2").convertToUnits(kMm);
            out.way3 = jf->locationOf("way-3").convertToUnits(kMm);
            out.boxDepth = jf->real("box-depth", -25);
            out.lastFeedDepth = jf->real("last-feed-depth", 0);
            out.throwAway = jf->number("throw-away-drop-box-content-after-failed-feeds", 9);
            out.vacuumDifference = jf->number("required-vacuum-difference", 150);
            out.poke = jf->childText("poke-for-parts", "false") == "true";
            if (const JPPart* p = m_config.part(out.partId)) out.partHeight = mm(p->height);
            f = out;
        });
        return f;
    }
    std::optional<JPDropBoxes::Box> box(const std::string& id) const {
        std::optional<JPDropBoxes::Box> b;
        main([&] { b = m_config.dropBoxes().box(id); });
        return b;
    }
    std::string lastHeap(const std::string& boxId) const {
        std::string h;
        main([&] {
            const auto& m = m_config.dropBoxes().lastHeap;
            if (const auto it = m.find(boxId); it != m.end()) h = it->second;
        });
        return h;
    }
    void setLastHeap(const std::string& boxId, const std::string& feederId) {
        main([&] {
            if (feederId.empty()) m_config.dropBoxes().lastHeap.erase(boxId);
            else m_config.dropBoxes().lastHeap[boxId] = feederId;
        });
    }
    void setLastFeedDepth(const std::string& feederId, double depth) {
        main([&] {
            if (JPFeeder* f = m_config.feeder(feederId)) f->setReal("last-feed-depth", depth);
        });
    }
    double partHeight(const std::string& partId) const {
        double h = 0;
        main([&] {
            if (const JPPart* p = m_config.part(partId)) h = mm(p->height);
        });
        return h;
    }

    // The nozzle with a tip `partId`'s package takes (its first, when the one on it does not fit).
    bool ensureTip(const std::string& partId, std::string& why) {
        std::vector<std::string> compatible;
        main([&] {
            if (const JPPart* p = m_config.part(partId))
                if (const JPPackage* pkg = m_config.package(p->packageId)) compatible = pkg->compatibleNozzleTipIds;
        });
        std::string on;
        for (const JPJobMachine::Nozzle& n : m_machine.nozzles())
            if (n.id == m_nozzleId) on = n.tipId;
        if (compatible.empty() || std::find(compatible.begin(), compatible.end(), on) != compatible.end()) return true;
        return m_machine.changeTip(m_nozzleId, compatible.front(), why);
    }
    JPJobMachine::Nozzle nozzle() const {
        for (const JPJobMachine::Nozzle& n : m_machine.nozzles())
            if (n.id == m_nozzleId) return n;
        return {};
    }

    // HeapFeederHelper.getNearestPart: the camera over `at`, the part found nearest it (turned as found), within the box.
    bool nearestPart(JPPipeline& pipeline, const JPLocation& at, const JPDropBoxes::Box& box, int showMs,
                     std::optional<JPLocation>& found, std::string& why) {
        found.reset();
        JPJobMachine::SeenRects seen;
        if (!m_machine.seeRects(at, pipeline, showMs, seen, why)) return false;
        double best = INFINITY;
        for (const JPJobMachine::SeenRects::Rect& r : seen.rects)
            if (const double d = std::hypot(r.x - at.x(), r.y - at.y()); d < best) {
                best = d;
                found = JPLocation(kMm, r.x, r.y, at.z(), r.pixelAngle);
            }
        if (found && found->linearDistanceTo(box.centerBottom) > kOutsideBoxMm) found.reset();
        return true;
    }

    // DropBox.getPartPickLocation: a part in the box, looked at again from over it twice, at the height of the part it holds.
    bool partInBox(const std::string& boxId, std::optional<JPLocation>& found, std::string& why) {
        found.reset();
        const auto b = box(boxId);
        std::optional<JPPipeline> pipeline;
        main([&] {
            pipeline = JPFeederPipelines::ofDropBox(m_config.dropBoxes(), boxId);
            if (pipeline) pipeline->context().configurationDirectory = m_config.directory();
        });
        if (!b || !pipeline) {
            why = "no drop box " + boxId;
            return false;
        }
        const JPLocation centre = b->centerBottom.convertToUnits(kMm).derive(std::nullopt, std::nullopt, std::nullopt, 0.0);
        if (!nearestPart(*pipeline, centre, *b, kLookShownMs, found, why)) return false;
        if (!found) return true;
        if (!nearestPart(*pipeline, found->derive(std::nullopt, std::nullopt, std::nullopt, 0.0), *b, kLastShownMs, found, why))
            return false;
        if (!found) {
            why = "DropBox " + b->name + ": Part is not detected again, check Pipeline";
            return false;
        }
        const std::string heap = lastHeap(boxId);
        std::string partId = b->dummyPartId;
        if (const auto h = heap.empty() ? std::nullopt : feeder(heap)) partId = h->partId;
        found = found->derive(std::nullopt, std::nullopt, centre.z() + partHeight(partId), std::nullopt);
        return true;
    }

    // A part picked at `at` (from safe Z, back up to it) as `partId`.
    bool pickPart(const JPLocation& at, const std::string& partId, std::string& why) {
        if (!m_machine.pick(m_nozzleId, at, why)) return false;
        m_machine.holding(m_nozzleId, partId);
        return true;
    }
    // HeapFeederHelper.dropPart: let go at `at`, blown off, checked gone.
    bool dropPart(const JPLocation& at, std::string& why) {
        if (m_machine.place(m_nozzleId, at, why)) {
            m_machine.holding(m_nozzleId, "");
            return true;
        }
        why = "HeapFeeder: Dropping part failed, check nozzle tip (" + why + ")";
        return false;
    }
    bool dropInto(const JPDropBoxes::Box& b, std::string& why) { return dropPart(b.drop.convertToUnits(kMm), why); }

    // The nozzle at safe Z along the heap's three moves, from the heap (`from`) or towards it, then down onto it.
    bool alongWays(const Feeder& h, bool from, std::string& why) {
        if (!m_machine.safeZ(why)) return false;
        const std::vector<JPLocation> ways = from ? std::vector { h.way1, h.way2, h.way3 } : std::vector { h.way3, h.way2, h.way1 };
        for (const JPLocation& w : ways)
            if (!m_machine.moveNozzle(m_nozzleId, { w.x(), w.y(), std::nullopt, std::nullopt }, 1.0, true, why)) return false;
        if (from) return true;
        return m_machine.moveNozzle(m_nozzleId, { h.location.x(), h.location.y(), h.location.z(), h.location.rotation() }, 1.0, false, why);
    }

    // DropBox.removePart: back to its heap, or (unknown) to the discard location.
    bool removePart(const std::string& boxId, const JPLocation& at, std::string& why) {
        const auto b = box(boxId);
        if (!b) return false;
        const std::string heap = lastHeap(boxId);
        const std::optional<Feeder> h = heap.empty() ? std::nullopt : feeder(heap);
        if (!h) {
            if (!ensureTip(b->dummyPartId, why) || !pickPart(at, b->dummyPartId, why)) return false;
            if (!m_machine.discard(m_nozzleId, why)) return false;
            m_machine.holding(m_nozzleId, "");
            return true;
        }
        if (!ensureTip(h->partId, why) || !pickPart(at, h->partId, why) || !m_machine.safeZ(why) || !alongWays(*h, false, why)
            || !dropPart(h->location, why))
            return false;
        // A little higher to pick from next time.
        setLastFeedDepth(h->id, h->lastFeedDepth - h->partHeight / 5);
        return true;
    }

    // DropBox.clean.
    bool clean(const std::string& boxId, std::string& why) {
        for (int i = 0; i < kMaxCleanAttempts; ++i) {
            std::optional<JPLocation> part;
            if (!partInBox(boxId, part, why)) return false;
            if (!part) {
                setLastHeap(boxId, "");
                return true;
            }
            if (!removePart(boxId, *part, why)) return false;
        }
        const auto b = box(boxId);
        why = "DropBox " + (b ? b->name : boxId) + ": Even after " + std::to_string(kMaxCleanAttempts)
              + " attempts the DropBox is not detected as empty. Check Pipeline.";
        return false;
    }

    // DropBox.tryToFlipSomePart: a part in the box picked and dropped again; false when there is none (or of no heap).
    bool tryToFlip(const std::string& boxId, bool& flipped, std::string& why) {
        flipped = false;
        std::optional<JPLocation> part;
        if (!partInBox(boxId, part, why)) return false;
        const std::string heap = lastHeap(boxId);
        const std::optional<Feeder> h = heap.empty() ? std::nullopt : feeder(heap);
        if (!part || !h) return true;
        const auto b = box(boxId);
        if (!pickPart(*part, h->partId, why) || !dropInto(*b, why)) return false;
        flipped = true;
        return true;
    }

    bool stable(double baseline, int difference, bool& reached, std::string& why) {
        reached = false;
        for (int i = 0; i < kStableReads; ++i) {
            double level = 0;
            if (!m_machine.readVacuum(m_nozzleId, level, why)) return false;
            if (std::abs(level - baseline) < difference) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(kStableReadMs));
        }
        reached = true;
        return true;
    }

    // ReferenceHeapFeeder.stirParts: round the heap's corners, a little lower each time, until the vacuum shows parts.
    bool stir(const Feeder& h, double baseline, double& depth, std::string& why) {
        const int dwell = nozzle().placeDwellMs;
        depth = h.lastFeedDepth + h.partHeight / 1.5;
        for (int i = 0;; ++i) {
            bool reached = false;
            if (!stable(baseline, h.vacuumDifference, reached, why)) return false;
            if (reached || depth <= h.boxDepth + h.partHeight || (depth <= h.lastFeedDepth - 3 * h.partHeight && h.lastFeedDepth != 0))
                break;
            depth -= std::min(kStirStepMm, h.partHeight / 10.0);
            const int corner = kCorners[i % 8];
            const double dx = corner == 0 || corner == 1 ? kCornerMm : -kCornerMm;
            const double dy = corner == 1 || corner == 2 ? kCornerMm : -kCornerMm;
            if (!m_machine.moveNozzle(m_nozzleId, { h.location.x() + dx, h.location.y() + dy, h.location.z() + depth, std::nullopt },
                                      kStirSpeed, false, why))
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(dwell / 3));
        }
        return grabbed(h, depth, why);
    }

    // ReferenceHeapFeeder.pokeForParts: down onto a grid of places, lifted between, until the vacuum shows parts.
    bool poke(const Feeder& h, double baseline, double& depth, std::string& why) {
        const int dwell = nozzle().placeDwellMs;
        const double step = std::max(kPokeStepMinMm, h.partHeight / 4.0);
        depth = h.lastFeedDepth + step;
        auto place = [&](int s, double z) {
            return std::array<std::optional<double>, 4> { h.location.x() + kPokeOriginMm + (s % kPokeGrid) * kPokeGridMm,
                                                           h.location.y() + kPokeOriginMm + (s / kPokeGrid) * kPokeGridMm,
                                                           h.location.z() + z, std::nullopt };
        };
        for (int i = 0;; i += 2) {
            bool reached = false;
            if (!stable(baseline, h.vacuumDifference, reached, why)) return false;
            if (reached || depth <= h.boxDepth + h.partHeight / 2
                || (depth <= h.lastFeedDepth - 2 * h.partHeight && h.lastFeedDepth != 0))
                break;
            const int s = i % kPokeCycle;
            if (s == 0 || s == 1) depth -= step;
            if (!m_machine.moveNozzle(m_nozzleId, place(s, depth), kPokeSpeed, false, why)
                || !m_machine.moveNozzle(m_nozzleId, place(s + 1, depth + std::max(kPokeLiftMinMm, kPokeLiftFactor * h.partHeight)),
                                         1.0, false, why))
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(dwell));
        }
        return grabbed(h, depth, why);
    }

    bool grabbed(const Feeder& h, double depth, std::string& why) {
        if (depth <= h.boxDepth + h.partHeight) {
            why = "HeapFeeder " + h.name + ": Can not grab parts. Heap Empty or VacuumDifference wrong.";
            return false;
        }
        if (depth <= h.lastFeedDepth - 3 * h.partHeight && h.lastFeedDepth != 0) {
            why = "HeapFeeder " + h.name + ": Can not grab parts. No parts found on three times part height.";
            return false;
        }
        return true;
    }

    // ReferenceHeapFeeder.fetchParts: parts taken from the heap with the vacuum, and dropped into the box.
    bool fetch(const Feeder& h, std::string& why) {
        if (!ensureTip(h.partId, why) || !m_machine.safeZ(why) || !m_machine.vacuumOn(m_nozzleId, why)) return false;
        const auto vacuumOn = std::chrono::steady_clock::now();
        if (!m_machine.moveNozzle(m_nozzleId, { h.location.x(), h.location.y(), std::nullopt, h.location.rotation() }, 1.0, true, why))
            return false;
        // The vacuum settled before its level is taken.
        const auto settled = vacuumOn + std::chrono::milliseconds(std::lround(kSettleFactor * nozzle().pickDwellMs));
        std::this_thread::sleep_until(settled);
        double baseline = 0;
        for (int i = 0; i < kBaselineReads; ++i) {
            double level = 0;
            if (!m_machine.readVacuum(m_nozzleId, level, why)) return false;
            baseline += level / kBaselineReads;
        }
        double depth = 0;
        if (!(h.poke ? poke(h, baseline, depth, why) : stir(h, baseline, depth, why))) return false;
        setLastFeedDepth(h.id, depth);
        if (!m_machine.pickHere(m_nozzleId, why)) return false;
        m_machine.holding(m_nozzleId, h.partId);
        const auto b = box(h.boxId);
        return b && m_machine.safeZ(why) && alongWays(h, true, why) && dropInto(*b, why);
    }

    // ReferenceHeapFeeder.getFeederPart: a part the right way up in the box, looked at three times.
    bool feederPart(const Feeder& h, std::optional<JPLocation>& pick, std::string& why) {
        const auto b = box(h.boxId);
        std::optional<JPPipeline> pipeline;
        main([&] {
            if (const JPFeeder* f = m_config.feeder(h.id)) {
                pipeline = JPFeederPipelines::of(*f, "feeder-pipeline", &m_config.dropBoxes());
                if (pipeline) {
                    pipeline->context().configurationDirectory = m_config.directory();
                    JPFeederPipelines::configureForEditing(m_config, *f, *pipeline);
                }
            }
        });
        if (!b || !pipeline) {
            why = "no feeder " + h.id;
            return false;
        }
        pick = b->centerBottom.convertToUnits(kMm).derive(std::nullopt, std::nullopt, std::nullopt, 0.0);
        for (int i = 0; i < kPartLooks && pick; ++i) {
            if (!nearestPart(*pipeline, pick->derive(std::nullopt, std::nullopt, std::nullopt, 0.0), *b,
                             i == kPartLooks - 1 ? kLastShownMs : kLookShownMs, pick, why))
                return false;
            if (pick) pick = pick->derive(std::nullopt, std::nullopt, b->centerBottom.convertToUnits(kMm).z() + h.partHeight, std::nullopt);
        }
        return true;
    }

    bool feed(std::string& why) {
        const std::optional<Feeder> h = feeder(m_feederId);
        if (!h) {
            why = "no feeder " + m_feederId;
            return false;
        }
        // Foreign parts in the box: cleaned out first; then it is this heap's.
        if (lastHeap(h->boxId) != h->id && !clean(h->boxId, why)) return false;
        setLastHeap(h->boxId, h->id);
        if (!ensureTip(h->partId, why)) return false;
        main([&] {
            if (JPFeeder* f = m_config.feeder(h->id)) f->foundPick.reset();
        });
        bool lastRoundFetched = false;
        for (int attempt = 0; attempt <= kMaxThrowRetries; ++attempt) {
            std::optional<JPLocation> pick;
            if (!feederPart(*h, pick, why)) return false;
            if (pick) {
                main([&] {
                    if (JPFeeder* f = m_config.feeder(h->id)) f->foundPick = pick;
                });
                return true;
            }
            // None the right way up: one turned by dropping it again, else more fetched.
            bool flipped = false;
            if (!tryToFlip(h->boxId, flipped, why)) return false;
            if (!flipped) {
                if (lastRoundFetched) {
                    why = "Feeder " + h->name + ": Fetching parts failed => feed failed.";
                    return false;
                }
                const std::optional<Feeder> now = feeder(m_feederId);
                if (!now || !fetch(*now, why)) return false;
                lastRoundFetched = true;
            } else {
                lastRoundFetched = false;
            }
            // Too many failed attempts: the box's parts thrown away (damaged or wrong, maybe).
            if (h->throwAway > 0 && attempt > 0 && attempt % h->throwAway == 0) {
                setLastHeap(h->boxId, "");
                if (!clean(h->boxId, why)) return false;
                setLastHeap(h->boxId, h->id);
            }
        }
        why = "Feeder " + h->name + ": No parts found.";
        return false;
    }

    bool samples(std::string& why) {
        const std::optional<Feeder> h = feeder(m_feederId);
        if (!h) {
            why = "no feeder " + m_feederId;
            return false;
        }
        if (!clean(h->boxId, why)) return false;
        setLastHeap(h->boxId, h->id);
        const std::optional<Feeder> now = feeder(m_feederId);
        if (!now || !fetch(*now, why)) return false;
        const auto b = box(h->boxId);
        return b && m_machine.positionCamera(b->centerBottom.derive(std::nullopt, std::nullopt, std::nullopt, 0.0), why);
    }

    bool cleanBox(std::string& why) {
        const std::optional<Feeder> h = feeder(m_feederId);
        if (!h) {
            why = "no feeder " + m_feederId;
            return false;
        }
        return clean(h->boxId, why);
    }

private:
    JPConfiguration&             m_config;
    std::string                  m_feederId, m_nozzleId;
    JPJobMachine&                m_machine;
    const JPHeapFeeder::OnMain& m_onMain;
};

} // namespace

bool JPHeapFeeder::feed(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId, JPJobMachine& machine,
                        const OnMain& onMain, std::string& why) {
    return Heap(config, feederId, nozzleId, machine, onMain).feed(why);
}

bool JPHeapFeeder::getSamples(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId,
                              JPJobMachine& machine, const OnMain& onMain, std::string& why) {
    return Heap(config, feederId, nozzleId, machine, onMain).samples(why);
}

bool JPHeapFeeder::cleanDropBox(JPConfiguration& config, const std::string& feederId, const std::string& nozzleId,
                                JPJobMachine& machine, const OnMain& onMain, std::string& why) {
    return Heap(config, feederId, nozzleId, machine, onMain).cleanBox(why);
}

} // inline namespace jf
