// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's stages that find things in the working image: contours (found,
// filtered, fitted with rectangles or ellipses), the circle enclosing the
// pixels in a range, and circles by the Hough transform.

#include "JPPipeline.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"

#include <opencv2/imgproc.hpp>

#include <cfloat>
#include <cmath>
#include <optional>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
using Model = JPPipelineModel;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

bool blank(const std::string& s) { return s.find_first_not_of(" \t") == std::string::npos; }

// A stage's contours, as OpenPnP's getExpectedListModel(MatOfPoint.class): none when it found nothing.
const Model::Contours* contoursOf(JPPipeline& p, const std::string& stageName) {
    const JPPipeline::Result& r = p.expectedResult(stageName);
    if (r.model.empty()) return nullptr;
    const auto* c = std::get_if<Model::Contours>(&r.model.value);
    if (!c)
        throw std::runtime_error("Pipeline stage \"" + stageName + "\" returned a " + r.model.kind() +
                                 " but expected a MatOfPoint list.");
    return c;
}

std::vector<cv::Point2f> floats(const std::vector<cv::Point>& c) { return { c.begin(), c.end() }; }

Model circles(const cv::Mat& found) {
    std::vector<Model::Circle> out;
    for (int i = 0; i < found.cols; ++i) {
        const cv::Vec3f c = found.at<cv::Vec3f>(0, i);
        out.push_back({ c[0], c[1], c[2] * 2.0 });
    }
    Model m;
    m.value = std::move(out);
    return m;
}

int retrieval(const std::string& mode) {
    if (mode == "ConnectedComponent") return cv::RETR_CCOMP;
    if (mode == "External") return cv::RETR_EXTERNAL;
    if (mode == "FloodFill") return cv::RETR_FLOODFILL;
    if (mode == "Tree") return cv::RETR_TREE;
    return cv::RETR_LIST;
}

int approximation(const std::string& method) {
    if (method == "Simple") return cv::CHAIN_APPROX_SIMPLE;
    if (method == "Tc89Kcos") return cv::CHAIN_APPROX_TC89_KCOS;
    if (method == "Tc89L1") return cv::CHAIN_APPROX_TC89_L1;
    return cv::CHAIN_APPROX_NONE;
}

// MinAreaRect's search with only some edges (OpenPnP's minAreaEdges): the
// angle whose bounding box, on the edges wanted, has the least area, searched
// in steps and again in finer ones; the points relative to their centre.
constexpr int    kStepsPerSearch = 18;
constexpr double kAngularResolution = 0.01;

std::optional<cv::RotatedRect> minAreaEdges(const std::vector<cv::Point2d>& points, bool left, bool right, bool top, bool bottom,
                                            double expectedAngle, double searchAngle, int cols, int rows) {
    double bestArea = INFINITY, bestAngle = NAN;
    std::optional<cv::RotatedRect> best;
    const double unbounded = std::hypot(cols, rows);
    const double step = searchAngle / kStepsPerSearch;
    const double da = step * CV_PI / 180;
    searchAngle = std::min(45.0, std::abs(searchAngle));
    for (double angle = (expectedAngle - searchAngle) * CV_PI / 180, most = (expectedAngle + searchAngle) * CV_PI / 180;
         angle < most; angle += da) {
        const double s = std::sin(angle), c = std::cos(angle);
        double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
        for (const cv::Point2d& p : points) {
            const double x = c * p.x - s * p.y, y = s * p.x + c * p.y;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
        // An edge not wanted stops at the centre of gravity.
        if (!left) x0 = 0;
        if (!right) x1 = 0;
        if (!top) y0 = 0;
        if (!bottom) y1 = 0;
        const double area = (x1 - x0) * (y1 - y0);
        if (bestArea > area) {
            bestArea = area;
            if (!left) x0 = -unbounded;
            if (!right) x1 = unbounded;
            if (!top) y0 = -unbounded;
            if (!bottom) y1 = unbounded;
            const double dx = (x0 + x1) / 2, dy = (y0 + y1) / 2;
            bestAngle = angle * 180 / CV_PI;
            best = cv::RotatedRect(cv::Point2f(float(c * dx + s * dy), float(-s * dx + c * dy)),
                                   cv::Size2f(float(x1 - x0), float(y1 - y0)), float(-bestAngle));
        }
    }
    if (step > kAngularResolution && !std::isnan(bestAngle))
        return minAreaEdges(points, left, right, top, bottom, bestAngle, step, cols, rows);
    return best;
}

bool flagOverride(JPPipeline& p, const JPPipelineStage& s, const std::string& attribute, const std::string& property) {
    const JPPipelineValue* v = p.property(property);
    if (!v) return s.flag(attribute);
    const bool* b = std::get_if<bool>(&v->value);
    if (!b) throw std::runtime_error("Pipeline property \"" + property + "\" must be of type \"java.lang.Boolean\"");
    return *b;
}

} // namespace

void JPStageRegistry::addDetectStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "FindContours", "", "",
                      { P { "retrieval-mode", Kind::Choice, "List", "", { "ConnectedComponent", "External", "FloodFill", "List", "Tree" } },
                        P { "approximation-method", Kind::Choice, "None", "", { "None", "Simple", "Tc89Kcos", "Tc89L1" } } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          Model::Contours contours;
                          cv::Mat image = p.workingImage().clone();   // findContours may change what it reads
                          cv::findContours(image, contours, retrieval(s.text("retrieval-mode")),
                                           approximation(s.text("approximation-method")));
                          Output out;
                          out.model.value = std::move(contours);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "FilterContours", "", "",
                      { P { "contours-stage-name", Kind::StageName, "", "" },
                        P { "min-area", Kind::Number, "-1.0", "Minimum area of the contour, or -1 if no minimum limit is wanted." },
                        P { "max-area", Kind::Number, "-1.0", "Maximum area of the contour, or -1 if no maximum limit is wanted." },
                        P { "property-name", Kind::Text, "FilterContours", "Name of the property through which OpenPnP controls this stage. Use \"FilterContours\" for standard control." } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const std::string from = s.text("contours-stage-name");
                          if (blank(from)) return Output {};
                          const Model::Contours* contours = contoursOf(p, from);
                          if (!contours) return Output {};
                          const std::string control = s.text("property-name");
                          const double minArea = p.overridden(s, "min-area", s.number("min-area"), control + ".minArea");
                          const double maxArea = p.overridden(s, "max-area", s.number("max-area"), control + ".maxArea");
                          Model::Contours kept;
                          for (const auto& c : *contours) {
                              const double area = cv::contourArea(c);
                              if (area >= (minArea == -1 ? DBL_MIN : minArea) && area <= (maxArea == -1 ? DBL_MAX : maxArea))
                                  kept.push_back(c);
                          }
                          Output out;
                          out.model.value = std::move(kept);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "MinAreaRectContours", "", "", { P { "contours-stage-name", Kind::StageName, "", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const std::string from = s.text("contours-stage-name");
                          if (blank(from)) return Output {};
                          const Model::Contours* contours = contoursOf(p, from);
                          if (!contours) return Output {};
                          std::vector<cv::RotatedRect> rects;
                          for (const auto& c : *contours) rects.push_back(cv::minAreaRect(floats(c)));
                          Output out;
                          out.model.value = std::move(rects);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "FitEllipseContours", "", "", { P { "contours-stage-name", Kind::StageName, "", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const std::string from = s.text("contours-stage-name");
                          if (blank(from)) return Output {};
                          const Model::Contours* contours = contoursOf(p, from);
                          if (!contours) return Output {};
                          std::vector<cv::RotatedRect> rects;
                          for (const auto& c : *contours) rects.push_back(cv::fitEllipseAMS(floats(c)));
                          Output out;
                          out.model.value = std::move(rects);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "MinEnclosingCircle", "", "",
                      { P { "threshold-min", Kind::Integer, "0", "" }, P { "threshold-max", Kind::Integer, "0", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          if (p.workingColorSpace() != "Gray")
                              throw std::runtime_error("MinEnclosingCircle is not compatible with " + p.workingColorSpace() +
                                                       " colorspace. Only Grey colorspace is supported.");
                          const cv::Mat& mat = p.workingImage();
                          const int lo = s.integer("threshold-min"), hi = s.integer("threshold-max");
                          std::vector<cv::Point2f> points;
                          for (int y = 0; y < mat.rows; ++y) {
                              const uchar* row = mat.ptr<uchar>(y);
                              for (int x = 0; x < mat.cols; ++x)
                                  if (row[x] >= lo && row[x] <= hi) points.emplace_back(float(x), float(y));
                          }
                          if (points.empty()) return Output {};
                          cv::Point2f center;
                          float radius = 0;
                          cv::minEnclosingCircle(points, center, radius);
                          Output out;
                          out.model.value = std::vector<Model::Circle> { { center.x, center.y, radius * 2.0 } };
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "MinAreaRect", "",
                      "Finds the smallest rotated rectangle that encloses pixels that fall within the given range.\nInput should be a grayscale image.",
                      { P { "threshold-min", Kind::Integer, "0", "Threshold minimum grayscale value of the pixel to be considered \"set\"." },
                        P { "threshold-max", Kind::Integer, "0", "Threshold mayimum grayscale value of the pixel to be considered \"set\"." },
                        P { "expected-angle", Kind::Number, "0.0", "Expected angle of the rectangular hull to be detected." },
                        P { "search-angle", Kind::Number, "45.0", "Search angle, two-sided around the expected angle." },
                        P { "left-edge", Kind::Flag, "true", "Detect the left edge of the rectangle (rotated to expectedAngle)." },
                        P { "right-edge", Kind::Flag, "true", "Detect the right edge of the rectangle (rotated to expectedAngle)." },
                        P { "top-edge", Kind::Flag, "true", "Detect the top edge of the rectangle (rotated to expectedAngle)." },
                        P { "bottom-edge", Kind::Flag, "true", "Detect the bottom edge of the rectangle (rotated to expectedAngle)." },
                        P { "diagnostics", Kind::Flag, "false", "Display the detection result diagnostics." },
                        P { "property-name", Kind::Text, "MinAreaRect",
                            "determines the pipeline property name under which this stage is controlled by the vision operation. If set, these will override some of the properties configured here. Use \"MinAreaRect\" for default control." } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          if (p.workingColorSpace() != "Gray")
                              throw std::runtime_error("MinAreaRect is not compatible with " + p.workingColorSpace() +
                                                       " colorspace. Only Grey colorspace is supported.");
                          cv::Mat& mat = p.workingImage();
                          double expected = s.number("expected-angle"), search = s.number("search-angle");
                          bool left = s.flag("left-edge"), right = s.flag("right-edge"), top = s.flag("top-edge"),
                               bottom = s.flag("bottom-edge");
                          const std::string control = s.text("property-name");
                          if (!control.empty()) {
                              expected = p.overridden(s, "expected-angle", expected, control + ".expectedAngle");
                              search = p.overridden(s, "search-angle", search, control + ".searchAngle");
                              left = flagOverride(p, s, "left-edge", control + ".leftEdge");
                              right = flagOverride(p, s, "right-edge", control + ".rightEdge");
                              top = flagOverride(p, s, "top-edge", control + ".topEdge");
                              bottom = flagOverride(p, s, "bottom-edge", control + ".bottomEdge");
                          }
                          // Only the first and last set pixels of each row and column stake out the hull.
                          const int cols = mat.cols, rows = mat.rows, lo = s.integer("threshold-min"), hi = s.integer("threshold-max");
                          std::vector<int> firstRow(size_t(cols), -1), lastRow(size_t(cols), 0);
                          long long xSum = 0, ySum = 0, n = 0;
                          for (int y = 0; y < rows; ++y) {
                              const uchar* row = mat.ptr<uchar>(y);
                              int first = -1, last = -1;
                              for (int x = 0; x < cols; ++x)
                                  if (row[x] >= lo && row[x] <= hi) {
                                      if (first < 0) first = x;
                                      last = x;
                                      xSum += x;
                                      ySum += y;
                                      ++n;
                                  }
                              if (first >= 0) {
                                  if (firstRow[size_t(first)] < 0) firstRow[size_t(first)] = y;
                                  if (firstRow[size_t(last)] < 0) firstRow[size_t(last)] = y;
                                  lastRow[size_t(first)] = y;
                                  lastRow[size_t(last)] = y;
                              }
                          }
                          // The pixels' centre of gravity: where a partial edge's rectangle snuggles to.
                          const cv::Point2d center(double(xSum) / double(n), double(ySum) / double(n));
                          const bool diagnostics = s.flag("diagnostics");
                          if (diagnostics) mat.setTo(cv::Scalar::all(0));
                          std::vector<cv::Point2d> points;
                          for (int x = 0; x < cols; ++x)
                              if (firstRow[size_t(x)] >= 0) {
                                  points.emplace_back(x, firstRow[size_t(x)]);
                                  points.emplace_back(x, lastRow[size_t(x)]);
                                  if (diagnostics) {
                                      mat.at<uchar>(firstRow[size_t(x)], x) = 255;
                                      mat.at<uchar>(lastRow[size_t(x)], x) = 255;
                                  }
                              }
                          if (points.empty()) return Output {};
                          Output out;
                          if (left && right && bottom && top && search >= 45 - kAngularResolution) {
                              std::vector<cv::Point2f> pf(points.begin(), points.end());
                              out.model.value = JPStageUtil::rotateToExpectedAngle(cv::minAreaRect(pf), expected);
                          } else {
                              for (cv::Point2d& pt : points) pt -= center;
                              if (auto r = minAreaEdges(points, left, right, top, bottom, expected, search, cols, rows))
                                  out.model.value = cv::RotatedRect(cv::Point2f(float(center.x + r->center.x), float(center.y + r->center.y)),
                                                                    r->size, r->angle);
                          }
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "DetectCirclesHough", "", "Finds circles in the working image. Diameter and spacing can be specified.",
                      { P { "min-distance", Kind::Integer, "10", "Minimum distance between circles, in pixels." },
                        P { "min-diameter", Kind::Integer, "10", "Minimum diameter of circles, in pixels." },
                        P { "max-diameter", Kind::Integer, "100", "Maximum diameter of circles, in pixels." },
                        P { "dp", Kind::Number, "1.0", "Inverse ratio of the accumulator resolution to the image resolution" },
                        P { "param1", Kind::Number, "80.0", "The higher threshold of the two passed to the Canny() edge detector (the lower one is twice smaller)" },
                        P { "param2", Kind::Number, "10.0", "The accumulator threshold for the circle centers at the detection stage. The smaller it is, the more false circles may be detected" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          cv::Mat found;
                          cv::HoughCircles(p.workingImage(), found, cv::HOUGH_GRADIENT, s.number("dp"), s.integer("min-distance"),
                                           s.number("param1"), s.number("param2"), s.integer("min-diameter") / 2,
                                           s.integer("max-diameter") / 2);
                          Output out;
                          out.model = circles(found);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "DetectFixedCirclesHough", "",
                      "Finds circles in the working image. Diameter and spacing are provided by the pipeline.",
                      { P { "dp", Kind::Number, "1.0", "Inverse ratio of the accumulator resolution to the image resolution" },
                        P { "param1", Kind::Number, "80.0", "The higher threshold of the two passed to the Canny() edge detector (the lower one is twice smaller)" },
                        P { "param2", Kind::Number, "13.0", "The accumulator threshold for the circle centers at the detection stage. The smaller it is, the more false circles may be detected" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          if (!p.context().capture && p.context().pixelsPerMmX <= 0) throw std::runtime_error("No Camera set on pipeline.");
                          auto integer = [&p](const char* name) -> std::optional<int> {
                              const JPPipelineValue* v = p.property(name);
                              if (!v) return std::nullopt;
                              if (const long* l = std::get_if<long>(&v->value)) return int(*l);
                              if (const double* d = std::get_if<double>(&v->value)) return int(std::lround(*d));
                              return std::nullopt;
                          };
                          const auto minDistance = integer("DetectFixedCirclesHough.minDistance");
                          const auto minDiameter = integer("DetectFixedCirclesHough.minDiameter");
                          const auto maxDiameter = integer("DetectFixedCirclesHough.maxDiameter");
                          if (!minDistance || !minDiameter || !maxDiameter)
                              throw std::runtime_error("DetectFixedCirclesHough properties are not set on pipeline.");
                          cv::Mat found;
                          cv::HoughCircles(p.workingImage(), found, cv::HOUGH_GRADIENT, s.number("dp"), *minDistance, s.number("param1"),
                                           s.number("param2"), *minDiameter / 2, *maxDiameter / 2);
                          Output out;
                          out.model = circles(found);
                          return out;
                      } });
}

} // inline namespace jf
