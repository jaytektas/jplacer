// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The heap feeders' drop boxes, as OpenPnP keeps them in machine.xml: read
// and written back as they were, a box added, renamed, placed and taken away
// (never the last), the colour its default pipelines are for.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPDropBoxes.h"
#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"

using namespace jf;

int main() {
    // None kept: a "Green" box, as OpenPnP makes it.
    JPDropBoxes fresh;
    assert(fresh.boxes().size() == 1 && fresh.boxes().front().name == "Green");
    assert(fresh.boxes().front().dummyPartId == "HeapFedder-Dummy");

    const std::string xml = R"(<entry><string>ReferenceHeapFeeder.dropBoxes</string><object class="org.openpnp.machine.reference.feeder.ReferenceHeapFeeder$DropBoxProperty"><boxes><drop-box id="DropBox-1" name="White" dummy-part-id-for-unknown="DUMMY"><part-pipeline><stages/></part-pipeline><center-bottom-location units="Millimeters" x="10.0" y="20.0" z="-30.0" rotation="0.0"/><drop-location units="Millimeters" x="11.0" y="21.0" z="-25.0" rotation="0.0"/></drop-box></boxes></object></entry>)";
    JPXmlElement e;
    std::string error;
    assert(JPXmlReader::parse(xml, e, error));
    auto boxes = JPDropBoxes::fromXml(e);
    assert(boxes && boxes->boxes().size() == 1);
    const JPDropBoxes::Box b = boxes->boxes().front();
    assert(b.id == "DropBox-1" && b.name == "White" && b.dummyPartId == "DUMMY");
    assert(b.centerBottom.x() == 10 && b.centerBottom.z() == -30 && b.drop.y() == 21);
    assert(boxes->partPipeline("DropBox-1") != nullptr);
    assert(JPDropBoxes::colour(b.name) == "WHITE" && JPDropBoxes::colour("Blue") == "GREEN");
    // Written back as read.
    assert(JPXmlWriter::text(boxes->toXml()).find("dummy-part-id-for-unknown=\"DUMMY\"") != std::string::npos);
    // Another property: not drop boxes.
    JPXmlElement other;
    assert(JPXmlReader::parse("<entry><string>SchultzFeederSlot.banks</string></entry>", other, error));
    assert(!JPDropBoxes::fromXml(other));

    // Added, renamed, placed; the only one never taken away.
    const std::string id = boxes->add();
    boxes->setName(id, "Black");
    boxes->setCenterBottom(id, JPLocation(JPLengthUnit::Millimeters, 1, 2, 3, 0));
    assert(boxes->box(id) && boxes->box(id)->name == "Black" && boxes->box(id)->centerBottom.y() == 2);
    std::string why;
    assert(boxes->remove("DropBox-1", why) && boxes->boxes().size() == 1);
    assert(!boxes->remove(id, why) && why == "Can't delete the only DropBox. There must always be one DropBox defined.");
    return 0;
}
