// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// What OpenPnP's push-pull feeders do across feeders: the tape width from
// the holes, parts compatible by package or tape and reel specification, the
// template ranked first, a template's settings cloned with its places moved
// to the other's tape, two feeders' places swapped, a new feeder next in a
// row, the OCR region kept, and the part OCR's text names.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "model/JPPushPullTemplates.h"

#include <cmath>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

namespace {

constexpr JPLengthUnit kMm = JPLengthUnit::Millimeters;
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

void part(JPConfiguration& c, const std::string& id, const std::string& package) {
    auto p = std::make_shared<JPPart>();
    p->id = id;
    p->packageId = package;
    c.addPart(p);
}

// A push-pull feeder for `partId`, its pick location at (x, 10) with the holes 3.5 mm above it (8 mm tape along +X).
std::string feeder(JPConfiguration& c, const std::string& partId, double x) {
    JPFeeder f = JPFeeder::create("org.openpnp.machine.reference.feeder.ReferencePushPullFeeder", partId);
    f.setLocation(JPLocation(kMm, x, 10, -5, 0));
    f.setLocationOf("hole-1-location", JPLocation(kMm, x - 2, 13.5, 0, 0));
    f.setLocationOf("hole-2-location", JPLocation(kMm, x + 2, 13.5, 0, 0));
    f.setEnabled(true);
    const std::string id = f.id();
    c.addFeeder(std::move(f));
    return id;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-push-pull-templates";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration c(dir.string());
    for (const auto& [id, tape] : { std::pair { "R0603", "8x4" }, std::pair { "C0603", "8x4" }, std::pair { "SOT23", "" } }) {
        auto pkg = std::make_shared<JPPackage>();
        pkg->id = id;
        if (*tape) pkg->tapeSpecification = tape;
        c.addPackage(pkg);
    }
    part(c, "R-10K", "R0603");
    part(c, "C-100N", "C0603");
    part(c, "Q-BC847", "SOT23");
    part(c, "R-1K", "R0603");

    // Compatible: the same package, or the same tape and reel specification.
    assert(JPPushPullTemplates::compatibleParts(c, "R-10K", "R-1K"));
    assert(JPPushPullTemplates::compatibleParts(c, "R-10K", "C-100N"));
    assert(!JPPushPullTemplates::compatibleParts(c, "R-10K", "Q-BC847"));
    assert(!JPPushPullTemplates::compatibleParts(c, "", "R-1K"));

    const std::string a = feeder(c, "R-10K", 0), b = feeder(c, "C-100N", 12), q = feeder(c, "Q-BC847", 24);
    // 8 mm tape: hole 3.5 mm off the part.
    assert(near(JPPushPullTemplates::tapeWidthMm(*c.feeder(a)), 8));
    // The template: a compatible part first, even farther than an incompatible one; then one marked a template.
    assert(JPPushPullTemplates::templateFeeder(c, a) == b);
    assert(JPPushPullTemplates::templateFeeder(c, q) == b);   // none compatible: the nearest of the most alike
    c.feeder(q)->setFlag("used-as-template", true);
    assert(JPPushPullTemplates::templateFeeder(c, q) == b);
    assert(JPPushPullTemplates::compatibleFeeders(c, a) == std::vector<std::string> { b });
    assert(JPPushPullTemplates::cloneTemplateStatus(c, a).find("selected by common tape & reel specification 8x4") != std::string::npos);
    assert(JPPushPullTemplates::cloneTemplateStatus(c, q).find("Clones to none.") == 0);

    // A clone: the template's lever places moved to this tape (12 mm along), its tape settings taken.
    JPFeeder* tb = c.feeder(b);
    tb->setLocationOf("feed-start-location", JPLocation(kMm, 14, 30, -3, 0));
    tb->setLocationOf("feed-end-location", JPLocation(kMm, 18, 30, -3, 0));
    tb->setNumber("feed-multiplier", 3);
    tb->setText("actuator-name", "Lever");
    JPPushPullTemplates::cloneSettings(c, a, b, { true, true, true, true, true });
    const JPFeeder* fa = c.feeder(a);
    assert(near(fa->locationOf("feed-start-location").x(), 2) && near(fa->locationOf("feed-end-location").x(), 6));
    assert(fa->number("feed-multiplier") == 3 && fa->text("actuator-name") == "Lever");
    assert(near(fa->location().x(), 0));   // its own pick location kept

    // Swapped: each at the other's place.
    JPPushPullTemplates::swapOut(c, a, b);
    assert(near(c.feeder(a)->location().x(), 12) && near(c.feeder(b)->location().x(), 0));
    assert(near(c.feeder(a)->locationOf("hole-1-location").x(), 10));
    JPPushPullTemplates::swapOut(c, a, b);

    // The next in the row: as far on as the nearest is.
    std::string fresh, why;
    assert(JPPushPullTemplates::createInRow(c, b, fresh, why));
    const JPFeeder* n = c.feeder(fresh);
    assert(n && n->typeName() == "ReferencePushPullFeeder" && near(n->location().x(), 24) && near(n->location().y(), 10));
    assert(near(n->locationOf("hole-2-location").x(), 26) && n->number("feed-multiplier") == 3);
    // Not in a row along X or Y: refused.
    c.feeder(q)->setLocation(JPLocation(kMm, 30, 20, -5, 0));
    c.removeFeeder(fresh);
    c.feeder(b)->setLocation(JPLocation(kMm, 12, 10, -5, 0));
    const std::string lone = feeder(c, "R-1K", 35);
    c.feeder(lone)->setLocation(JPLocation(kMm, 35, 25, -5, 0));
    assert(!JPPushPullTemplates::createInRow(c, lone, fresh, why) && why.find("does not form a row in X and Y") != std::string::npos);

    // The OCR region, kept and read back.
    JPPushPullTemplates::OcrRegion r;
    r.upperLeft = JPLocation(kMm, -2, 1, 0, 0);
    r.upperRight = JPLocation(kMm, 2, 1, 0, 0);
    r.lowerLeft = JPLocation(kMm, -2, -1, 0, 0);
    r.rectify = true;
    JPPushPullTemplates::setOcrRegion(*c.feeder(a), r);
    const auto back = JPPushPullTemplates::ocrRegion(*c.feeder(a));
    assert(back && back->rectify && near(back->upperRight.x(), 2) && !back->offsets);

    // OCR's text: the first line, up to a space; whole, or the end of a part id after "-".
    std::string id;
    assert(JPPushPullTemplates::identifyPart(c, "R-10K\nsomething", 0.9, "F", id, why) && id == "R-10K");
    assert(JPPushPullTemplates::identifyPart(c, "BC847 rest", 0.9, "F", id, why) && id == "Q-BC847");
    assert(!JPPushPullTemplates::identifyPart(c, "XYZ", 0.5, "F", id, why));
    assert(why == "OCR could not identify/find part id on feeder F, OCR detected part id XYZ (avg. score=0.5)");
    part(c, "S-10K", "R0603");
    assert(!JPPushPullTemplates::identifyPart(c, "10K", 0.9, "F", id, why) && why.find("matches multiple parts") != std::string::npos);
    // Every character of the parts, no spaces.
    const std::string alphabet = JPPushPullTemplates::partsAlphabet(c, "\\");
    assert(alphabet.find('\\') != std::string::npos && alphabet.find('Q') != std::string::npos && alphabet.find(' ') == std::string::npos);
    return 0;
}
