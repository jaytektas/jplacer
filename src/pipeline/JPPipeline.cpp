// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipeline.h"

#include "JPStageRegistry.h"

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <sstream>

inline namespace jf {

namespace {

// OpenPnP's picture when there is none: black, a red cross corner to corner.
constexpr int kBlankW = 640, kBlankH = 480;

std::string shown(double v) {
    std::ostringstream s;
    s << v;
    return s.str();
}

} // namespace

JPPipeline JPPipeline::fromXml(const JPXmlElement& cvPipeline) {
    JPPipeline p;
    const JPXmlElement* stages = cvPipeline.child("stages");
    if (stages)
        for (const JPXmlElement& e : stages->children)
            if (e.name == "cv-stage") p.m_stages.push_back(JPPipelineStage::fromXml(e));
    return p;
}

JPXmlNode JPPipeline::toXml() const {
    JPXmlNode root("cv-pipeline");
    JPXmlNode& stages = root.add(JPXmlNode("stages"));
    for (const JPPipelineStage& s : m_stages) stages.add(s.toXml());
    return root;
}

JPPipelineStage* JPPipeline::stage(const std::string& name) {
    for (JPPipelineStage& s : m_stages)
        if (s.name() == name) return &s;
    return nullptr;
}

std::string JPPipeline::uniqueName() const {
    for (int i = 0;; ++i) {
        const std::string name = std::to_string(i);
        bool taken = false;
        for (const JPPipelineStage& s : m_stages) taken = taken || s.name() == name;
        if (!taken) return name;
    }
}

cv::Mat& JPPipeline::workingImage() {
    if (m_working.empty()) {
        m_working = cv::Mat(kBlankH, kBlankW, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::line(m_working, { 0, 0 }, { kBlankW, kBlankH }, cv::Scalar(0, 0, 255));
        cv::line(m_working, { kBlankW, 0 }, { 0, kBlankH }, cv::Scalar(0, 0, 255));
        m_colorSpace = "Bgr";
    }
    return m_working;
}

const JPPipelineValue* JPPipeline::property(const std::string& name) const {
    const auto i = m_properties.find(name);
    return i == m_properties.end() ? nullptr : &i->second;
}

bool JPPipeline::process(std::string& why) {
    m_results.clear();
    m_overrides.clear();
    m_working.release();
    m_colorSpace.clear();
    m_model = {};
    m_totalMs = 0;
    std::string terminal;
    for (JPPipelineStage& stage : m_stages) {
        const auto start = std::chrono::steady_clock::now();
        JPStageType::Output out;
        try {
            if (!stage.enabled()) throw std::runtime_error("Stage \"" + stage.name() + "\"not enabled.");
            const JPStageType* type = JPStageRegistry::instance().find(stage.className());
            if (!type) throw std::runtime_error("Stage class " + stage.className() + " is not known to jplacer.");
            out = type->process(*this, stage);
        } catch (const Terminal& e) {
            out = {};
            out.model.value = JPPipelineModel::Failure { e.what() };
            terminal = e.what();
        } catch (const std::exception& e) {
            out = {};
            out.model.value = JPPipelineModel::Failure { e.what() };
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        m_totalMs += ms;
        if (stage.enabled() && !out.model.empty()) m_model = out.model;
        if (stage.enabled() && out.colorSpace) m_colorSpace = *out.colorSpace;
        Result r;
        if (out.image.empty()) {
            if (!m_working.empty()) r.image = m_working.clone();
        } else {
            m_working = out.image;
            r.image = out.image.clone();
        }
        r.colorSpace = out.colorSpace ? *out.colorSpace : m_colorSpace;
        r.model = std::move(out.model);
        r.milliseconds = ms;
        m_results[stage.name()] = std::move(r);
    }
    if (terminal.empty()) return true;
    why = terminal;
    return false;
}

const JPPipeline::Result* JPPipeline::result(const std::string& stageName) const {
    const auto i = m_results.find(stageName);
    return i == m_results.end() ? nullptr : &i->second;
}

const JPPipeline::Result& JPPipeline::expectedResult(const std::string& stageName) const {
    if (stageName.empty()) throw std::runtime_error("Stage name must be given.");
    bool there = false;
    for (const JPPipelineStage& s : m_stages) there = there || s.name() == stageName;
    if (!there) throw std::runtime_error("Stage \"" + stageName + "\" is missing in the pipeline.");
    const Result* r = result(stageName);
    if (!r) throw std::runtime_error("Stage \"" + stageName + "\" returned no result.");
    if (const auto* f = r->model.failure()) throw std::runtime_error(f->message);
    return *r;
}

double JPPipeline::overridden(const JPPipelineStage& stage, const std::string& attribute, double value,
                              const std::string& pipelineProperty) {
    const JPPipelineValue* v = property(pipelineProperty);
    if (!v) return value;
    const double pxPerMm = (m_context.pixelsPerMmX + m_context.pixelsPerMmY) / 2;
    auto needCamera = [&] {
        if (pxPerMm <= 0) throw std::runtime_error("Unable to convert to pixels because pipeline property \"camera\" is not set");
    };
    double out = value;
    if (const double* d = std::get_if<double>(&v->value)) out = *d;
    else if (const long* l = std::get_if<long>(&v->value)) out = double(*l);
    else if (const auto* len = std::get_if<JPPipelineValue::LengthMm>(&v->value)) {
        needCamera();
        out = len->mm * pxPerMm;
    } else if (const auto* area = std::get_if<JPPipelineValue::AreaMm2>(&v->value)) {
        needCamera();
        out = area->mm2 * m_context.pixelsPerMmX * m_context.pixelsPerMmY;
    } else {
        throw std::runtime_error("Pipeline property \"" + pipelineProperty + "\" must be a number, a length or an area");
    }
    m_overrides[stage.name()][attribute] = shown(out);
    return out;
}

cv::Point2d JPPipeline::overriddenPoint(const JPPipelineStage& stage, const std::string& attribute, cv::Point2d value,
                                        const std::string& pipelineProperty) {
    const JPPipelineValue* v = property(pipelineProperty);
    if (!v) return value;
    cv::Point2d out = value;
    if (const auto* p = std::get_if<JPPipelineValue::Pixel>(&v->value)) out = { p->x, p->y };
    else if (const auto* l = std::get_if<JPPipelineValue::LocationMm>(&v->value)) {
        if (!m_context.locationToPixel || !m_context.locationToPixel(l->x, l->y, out.x, out.y))
            throw std::runtime_error("Unable to convert to pixels because pipeline property \"camera\" is not set");
    } else {
        throw std::runtime_error("Pipeline property \"" + pipelineProperty + "\" must be a point or a location");
    }
    m_overrides[stage.name()][attribute] = "{" + shown(out.x) + ", " + shown(out.y) + "}";
    return out;
}

std::string JPPipeline::overriddenText(const JPPipelineStage& stage, const std::string& attribute, const std::string& value,
                                       const std::string& pipelineProperty) {
    const JPPipelineValue* v = property(pipelineProperty);
    if (!v) return value;
    const std::string* t = std::get_if<std::string>(&v->value);
    if (!t) throw std::runtime_error("Pipeline property \"" + pipelineProperty + "\" must be a text");
    m_overrides[stage.name()][attribute] = *t;
    return *t;
}

std::map<std::string, std::string> JPPipeline::overrides(const std::string& stageName) const {
    const auto i = m_overrides.find(stageName);
    return i == m_overrides.end() ? std::map<std::string, std::string> {} : i->second;
}

} // inline namespace jf
