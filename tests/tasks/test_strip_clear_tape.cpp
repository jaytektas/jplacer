// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Clear tape, as the bench's top camera saw it (tests/data/strip-clear-tape.png: its straightened 1280 x 720
// picture in grey, 25.2 px/mm at the tape): the sprocket holes are pale discs on glare, one far cleaner than the
// rest. The strip pipeline's circle finder kept only marks scoring at least corr-symmetry times the best one;
// at OpenPnP's 0.2 the hole at y 414 was dropped, and with it missing, OpenPnP's unbroken run of holes found no
// line at all. At jplacer's 0.05 all five are found, and the line through them with them.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPConfiguration.h"
#include "model/JPFeeder.h"
#include "openpnp/JPXmlReader.h"
#include "pipeline/JPDefaultPipelines.h"
#include "pipeline/JPPipeline.h"
#include "tasks/JPFeederPipelines.h"
#include "tasks/JPStripHoles.h"

#include <cmath>
#include <string>

using namespace jf;

namespace {

constexpr double kPxPerMm = 25.2;

// The default strip pipeline on the picture, its capture replaced by reading it; its corr-symmetry as given (empty:
// as jplacer ships it).
std::vector<JPStripHoles::Circle> circles(const std::string& corrSymmetry) {
    std::string xml = JPDefaultPipelines::stripFeeder();
    const std::string capture = R"(<cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="true" count="1"/>)";
    xml.replace(xml.find(capture), capture.size(),
                std::string(R"(<cv-stage class="org.openpnp.vision.pipeline.stages.ImageRead" name="0" enabled="true" file=")")
                    + JPLACER_TESTDATA_DIR + "/strip-clear-tape.png\"/>");
    if (!corrSymmetry.empty()) {
        const std::string corr = "corr-symmetry=\"";
        const size_t at = xml.find(corr) + corr.size();
        xml.replace(at, xml.find('"', at) - at, corrSymmetry);
    }
    JPXmlElement root;
    std::string why;
    assert(JPXmlReader::parse(xml, root, why));
    JPPipeline p = JPPipeline::fromXml(root);
    p.context().cameraWidth = 1280;
    p.context().cameraHeight = 720;
    p.context().pixelsPerMmX = p.context().pixelsPerMmY = kPxPerMm;
    JPConfiguration config("/tmp");
    JPFeederPipelines::configureForEditing(config, JPFeeder::create("org.openpnp.machine.reference.feeder.ReferenceStripFeeder", ""), p);
    assert(p.process(why));
    std::vector<JPStripHoles::Circle> out;
    if (const auto* l = std::get_if<std::vector<JPPipelineModel::Circle>>(&p.result("results")->model.value))
        for (const auto& c : *l) out.push_back({ c.x, c.y, c.diameter });
    return out;
}

bool has(const std::vector<JPStripHoles::Circle>& cs, double x, double y) {
    for (const auto& c : cs) if (std::hypot(c.x - x, c.y - y) < 5) return true;
    return false;
}

} // namespace

int main() {
    // The holes down the strip's left (x 562); the camera was over the part, the picture's middle.
    const double holeX = 562, ys[] = { 212, 312, 414, 513, 616 };
    const std::vector<JPStripHoles::Circle> openpnps = circles("0.2");
    assert(!has(openpnps, holeX, 414));
    const std::vector<JPStripHoles::Circle> found = circles("");
    for (const double y : ys) assert(has(found, holeX, y));
    const JPStripHoles::Result r = JPStripHoles::find(found, { 640, 360 }, kPxPerMm, 8);
    assert(r.hasBest && r.inLine.size() >= 5);
    return 0;
}
