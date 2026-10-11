// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// An OpenPnP-shaped element in jplacer's JSON (JPXmlJson): attributes as typed values, a child as an object,
// children of one name as an array, text under "#text"; and back to the same XML (a whole number as an integer).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPXmlJson.h"
#include "openpnp/JPXmlReader.h"
#include "openpnp/JPXmlWriter.h"

using namespace jf;

namespace {

JPXmlNode parsed(const std::string& xml) {
    JPXmlElement e;
    std::string error;
    assert(JPXmlReader::parse(xml, e, error));
    return JPXmlNode::from(e);
}

} // namespace

int main() {
    const JPXmlNode feeder = parsed(
        "<feeder class=\"org.openpnp.machine.reference.feeder.ReferenceStripFeeder\" id=\"FDR1\" enabled=\"true\" "
        "part-id=\"0123\" feed-count=\"3\" tape-width=\"8.0\" offset=\"1.0E-4\" name=\"1.5\">"
        "<location units=\"Millimeters\" x=\"10.25\" y=\"-2.0\" z=\"0.0\" rotation=\"90.0\"/>"
        "<parallax-angle>0.0</parallax-angle>"
        "<vision enabled=\"false\"><pipeline><stages>"
        "<cv-stage class=\"A\" name=\"0\"/><cv-stage class=\"B\" name=\"1\" kernel-size=\"3\"/>"
        "</stages></pipeline></vision>"
        "<ids><string>Brd1</string><string>Brd2</string></ids>"
        "<empty/>"
        "</feeder>");
    const JJson j = JPXmlJson::from(feeder);

    // Attributes typed: booleans, integers, Java doubles; an id with a leading zero stays text.
    assert(j["enabled"].isBool() && j["enabled"].boolean());
    assert(j["feed-count"].isNumber() && j["feed-count"].number() == 3);
    assert(j["tape-width"].isNumber() && j["tape-width"].number() == 8.0);
    assert(j["offset"].isNumber() && j["offset"].number() == 1.0e-4);
    assert(j["part-id"].isString() && j["part-id"].str() == "0123");
    assert(j["class"].str() == "org.openpnp.machine.reference.feeder.ReferenceStripFeeder");
    // The attributes first, in the element's order.
    assert(j.obj().front().first == "class" && j.obj()[1].first == "id");
    // A child an object, a text-only child its "#text", repeated children an array (text-only ones their text).
    assert(j["location"]["x"].number() == 10.25 && j["location"]["units"].str() == "Millimeters");
    assert(j["parallax-angle"]["#text"].number() == 0.0);
    const JJson& stages = j["vision"]["pipeline"]["stages"]["cv-stage"];
    assert(stages.isArray() && stages.size() == 2 && stages[1]["kernel-size"].number() == 3);
    assert(j["ids"]["string"].isArray() && j["ids"]["string"][1].str() == "Brd2");
    assert(j["empty"].isObject() && j["empty"].obj().empty());
    // The file's layout between children is not text.
    const JJson laidOut = JPXmlJson::from(parsed("<a>\n   <b x=\"1\"/>\n</a>"));
    assert(!laidOut.contains("#text") && laidOut["b"]["x"].number() == 1);

    // Back: the same element, a whole number written as an integer.
    const std::string back = JPXmlWriter::text(JPXmlJson::node("feeder", JJson::parse(j.dump(2))));
    const std::string want = JPXmlWriter::text(parsed(
        "<feeder class=\"org.openpnp.machine.reference.feeder.ReferenceStripFeeder\" id=\"FDR1\" enabled=\"true\" "
        "part-id=\"0123\" feed-count=\"3\" tape-width=\"8\" offset=\"1.0E-4\" name=\"1.5\">"
        "<location units=\"Millimeters\" x=\"10.25\" y=\"-2\" z=\"0\" rotation=\"90\"/>"
        "<parallax-angle>0</parallax-angle>"
        "<vision enabled=\"false\"><pipeline><stages>"
        "<cv-stage class=\"A\" name=\"0\"/><cv-stage class=\"B\" name=\"1\" kernel-size=\"3\"/>"
        "</stages></pipeline></vision>"
        "<ids><string>Brd1</string><string>Brd2</string></ids>"
        "<empty/>"
        "</feeder>"));
    assert(back == want);

    // An attribute and a child of one name: the children kept in "#children", in order, and back the same.
    const JPXmlNode clash = parsed("<entry location=\"a\"><location x=\"1.0\"/><b/><location x=\"2.5\"/></entry>");
    const JJson c = JPXmlJson::from(clash);
    assert(c["location"].str() == "a" && c["#children"].size() == 3 && c["#children"][1]["#name"].str() == "b");
    assert(JPXmlWriter::text(JPXmlJson::node("entry", c))
           == JPXmlWriter::text(parsed("<entry location=\"a\"><location x=\"1\"/><b/><location x=\"2.5\"/></entry>")));
    return 0;
}
