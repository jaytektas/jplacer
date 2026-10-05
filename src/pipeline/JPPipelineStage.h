// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <opencv2/core.hpp>

#include <string>

inline namespace jf {

// One stage of a vision pipeline (OpenPnP's <cv-stage class="…" name="…"
// enabled="…" …/>), kept as its XML node and changed in place, so it is
// written back as OpenPnP wrote it. Its settings are read by their XML names
// ("kernel-size"); one not written is the stage type's default (JPStageType).
class JPPipelineStage {
public:
    explicit JPPipelineStage(JPXmlNode node) : m_node(std::move(node)) {}
    static JPPipelineStage fromXml(const JPXmlElement& e) { return JPPipelineStage(JPXmlNode::from(e)); }
    // A new stage of an OpenPnP class, named `name`, with its type's defaults.
    static JPPipelineStage create(const std::string& className, const std::string& name);
    const JPXmlNode& toXml() const { return m_node; }

    std::string className() const;
    // Its class's simple name ("BlurGaussian").
    std::string typeName() const;
    std::string name() const;
    void        setName(const std::string& n) { m_node.set("name", n); }
    bool        enabled() const;
    void        setEnabled(bool on) { m_node.set("enabled", on ? "true" : "false"); }

    // A setting as written; its type's default when not written.
    std::string text(const std::string& attribute) const;
    double      number(const std::string& attribute) const;
    int         integer(const std::string& attribute) const;
    bool        flag(const std::string& attribute) const;
    void        set(const std::string& attribute, const std::string& value) { m_node.set(attribute, value); }
    // A colour child element (<color r g b a/>), as BGR(A) for OpenCV; its default when not written.
    cv::Scalar  color(const std::string& element) const;
    bool        hasColor(const std::string& element) const { return m_node.child(element) != nullptr; }
    void        setColor(const std::string& element, int r, int g, int b, int a);

private:
    std::string defaultOf(const std::string& attribute) const;

    JPXmlNode m_node;
};

} // inline namespace jf
