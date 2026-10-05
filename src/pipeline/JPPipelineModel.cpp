// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPipelineModel.h"

#include <cstdio>
#include <sstream>

inline namespace jf {

namespace {

// As OpenPnP's OpenCV Java classes print themselves.
std::string point(const cv::Point2d& p) {
    std::ostringstream s;
    s << "{" << p.x << ", " << p.y << "}";
    return s.str();
}

std::string rect(const cv::RotatedRect& r) {
    std::ostringstream s;
    s << "{ " << point(r.center) << " " << r.size.width << "x" << r.size.height << " * " << r.angle << " }";
    return s.str();
}

std::string match(const JPPipelineModel::TemplateMatch& m) {
    std::ostringstream s;
    s << "TemplateMatch [x=" << m.x << ", y=" << m.y << ", width=" << m.width << ", height=" << m.height << ", score=" << m.score
      << "]";
    return s.str();
}

template <typename T, typename F>
std::string list(const std::vector<T>& items, F one) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) out += (i ? ", " : "") + one(items[i]);
    return out + "]";
}

} // namespace

std::string JPPipelineModel::describe() const {
    struct Visitor {
        std::string operator()(std::monostate) const { return {}; }
        std::string operator()(const cv::RotatedRect& r) const { return rect(r); }
        std::string operator()(const std::vector<cv::RotatedRect>& v) const { return list(v, rect); }
        std::string operator()(const std::vector<Circle>& v) const {
            return list(v, [](const Circle& c) {
                std::ostringstream s;
                s << "Circle [x=" << c.x << ", y=" << c.y << ", diameter=" << c.diameter << "]";
                return s.str();
            });
        }
        std::string operator()(const std::vector<cv::KeyPoint>& v) const {
            return list(v, [](const cv::KeyPoint& k) {
                std::ostringstream s;
                s << "KeyPoint [pt=" << point(k.pt) << ", size=" << k.size << ", angle=" << k.angle << ", response="
                  << k.response << "]";
                return s.str();
            });
        }
        std::string operator()(const Contours& v) const { return std::to_string(v.size()) + " contours"; }
        std::string operator()(const std::vector<Line>& v) const {
            return list(v, [](const Line& l) { return "Line [a=" + point(l.a) + ", b=" + point(l.b) + "]"; });
        }
        std::string operator()(const std::vector<TemplateMatch>& v) const { return list(v, match); }
        std::string operator()(const TemplateMatch& m) const { return match(m); }
        std::string operator()(const cv::Matx23d& t) const {
            std::ostringstream s;
            s << "AffineTransform[[" << t(0, 0) << ", " << t(0, 1) << ", " << t(0, 2) << "], [" << t(1, 0) << ", " << t(1, 1)
              << ", " << t(1, 2) << "]]";
            return s.str();
        }
        std::string operator()(const std::vector<cv::Point2d>& v) const { return list(v, point); }
        std::string operator()(const cv::Point2d& p) const { return point(p); }
        std::string operator()(double d) const {
            std::ostringstream s;
            s << d;
            return s.str();
        }
        std::string operator()(const std::string& t) const { return t; }
        std::string operator()(const Failure& f) const { return f.message; }
        std::string operator()(const cv::KeyPoint& k) const { return (*this)(std::vector<cv::KeyPoint> { k }); }
        std::string operator()(const Circle& c) const {
            std::ostringstream s;
            s << "Circle [x=" << c.x << ", y=" << c.y << ", diameter=" << c.diameter << "]";
            return s.str();
        }
    };
    return std::visit(Visitor {}, value);
}

std::string JPPipelineModel::kind() const {
    switch (value.index()) {
        case 0: return "nothing";
        case 1: return "RotatedRect";
        case 2: return "RotatedRect list";
        case 3: return "Circle list";
        case 4: return "KeyPoint list";
        case 5: return "MatOfPoint list";
        case 6: return "Line list";
        case 7: return "TemplateMatch list";
        case 8: return "Point list";
        case 9: return "Point";
        case 10: return "Double";
        case 11: return "String";
        case 12: return "Exception";
        case 13: return "KeyPoint";
        case 14: return "Circle";
        case 15: return "TemplateMatch";
        default: return "AffineTransform";
    }
}

} // inline namespace jf
