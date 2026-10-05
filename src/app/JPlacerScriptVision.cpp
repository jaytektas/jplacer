// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPlacerScriptVision.h"

#include "openpnp/JPXmlReader.h"
#include "pipeline/JPPipeline.h"

#include <opencv2/imgproc.hpp>

inline namespace jf {

namespace {

JJson point(double x, double y) {
    JJson o = JJson::object();
    o["x"] = x;
    o["y"] = y;
    return o;
}

// A model as OpenPnP's scripts see it: an OpenCV KeyPoint's pt and size, a
// RotatedRect's center, size and angle, a circle's x, y and diameter.
JJson valueOf(const JPPipelineModel& m) {
    using M = JPPipelineModel;
    auto keyPoint = [](const cv::KeyPoint& k) {
        JJson o = JJson::object();
        o["pt"] = point(k.pt.x, k.pt.y);
        o["size"] = k.size;
        o["angle"] = k.angle;
        o["response"] = k.response;
        return o;
    };
    auto rect = [](const cv::RotatedRect& r) {
        JJson o = JJson::object();
        o["center"] = point(r.center.x, r.center.y);
        JJson size = JJson::object();
        size["width"] = r.size.width;
        size["height"] = r.size.height;
        o["size"] = size;
        o["angle"] = r.angle;
        return o;
    };
    auto circle = [](const M::Circle& c) {
        JJson o = point(c.x, c.y);
        o["diameter"] = c.diameter;
        return o;
    };
    JJson out;
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, cv::RotatedRect>) out = rect(v);
            else if constexpr (std::is_same_v<T, std::vector<cv::RotatedRect>>) {
                out = JJson::array();
                for (const auto& r : v) out.push(rect(r));
            } else if constexpr (std::is_same_v<T, std::vector<M::Circle>>) {
                out = JJson::array();
                for (const auto& c : v) out.push(circle(c));
            } else if constexpr (std::is_same_v<T, M::Circle>) out = circle(v);
            else if constexpr (std::is_same_v<T, std::vector<cv::KeyPoint>>) {
                out = JJson::array();
                for (const auto& k : v) out.push(keyPoint(k));
            } else if constexpr (std::is_same_v<T, cv::KeyPoint>) out = keyPoint(v);
            else if constexpr (std::is_same_v<T, std::vector<cv::Point2d>>) {
                out = JJson::array();
                for (const auto& p : v) out.push(point(p.x, p.y));
            } else if constexpr (std::is_same_v<T, cv::Point2d>) out = point(v.x, v.y);
            else if constexpr (std::is_same_v<T, double>) out = v;
            else if constexpr (std::is_same_v<T, std::string>) out = v;
            else if constexpr (std::is_same_v<T, M::Ocr>) out = v.text;
        },
        m.value);
    return out;
}

} // namespace

bool JPlacerScriptVision::run(JPJobMachine& machine, const JJson& request, JJson& result, std::string& why) {
    JPXmlElement root;
    if (!JPXmlReader::parse(request["xml"].str(), root, why)) return false;
    if (root.name != "cv-pipeline") {
        why = "not a pipeline (<cv-pipeline>)";
        return false;
    }
    JPPipeline pipeline = JPPipeline::fromXml(root);
    const auto at = machine.cameraLocation();
    if (!at) {
        why = "where the camera is is not known";
        return false;
    }
    JPJobMachine::Sight sight;
    if (!machine.lookThrough(*at, pipeline, sight, why)) return false;
    JJson results = JJson::object();
    for (const JPPipelineStage& stage : pipeline.stages()) {
        const JPPipeline::Result* r = pipeline.result(stage.name());
        if (!r) continue;
        JJson o = JJson::object();
        o["kind"] = r->model.kind();
        o["text"] = r->model.describe();
        o["value"] = valueOf(r->model);
        results[stage.name()] = o;
    }
    result = JJson::object();
    result["results"] = results;
    {
        std::lock_guard lk(m_mutex);
        const cv::Mat& image = pipeline.workingImage();
        if (image.channels() == 1) cv::cvtColor(image, m_last, cv::COLOR_GRAY2BGR);
        else image.copyTo(m_last);
    }
    return true;
}

bool JPlacerScriptVision::show(JPJobMachine& machine, int ms, std::string& why) {
    cv::Mat image;
    {
        std::lock_guard lk(m_mutex);
        image = m_last.clone();
    }
    if (image.empty()) {
        why = "no pipeline has been run to show";
        return false;
    }
    machine.showOnCamera(image, ms);
    return true;
}

} // inline namespace jf
