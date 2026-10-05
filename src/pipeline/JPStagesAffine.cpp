// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's AffineWarp and AffineUnwarp: a region of the picture, given in
// lengths about the camera's centre, cut out turned, scaled or sheared; and
// what a later stage found in it taken back to the camera's pixels.

#include "JPPipeline.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <sstream>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
using Model = JPPipelineModel;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

// Millimetres in one of OpenPnP's length units.
double mmPer(const std::string& unit) {
    static const std::pair<const char*, double> units[] = { { "Meters", 1000 }, { "Centimeters", 10 }, { "Millimeters", 1 },
                                                            { "Feet", 304.8 },  { "Inches", 25.4 },    { "Mils", 0.0254 },
                                                            { "Microns", 0.001 } };
    for (const auto& [n, mm] : units)
        if (unit == n) return mm;
    return 1;
}

std::string shown(double v) {
    std::ostringstream s;
    s << v;
    return s.str();
}

cv::Point2d apply(const cv::Matx33d& t, double x, double y) {
    return { t(0, 0) * x + t(0, 1) * y + t(0, 2), t(1, 0) * x + t(1, 1) * y + t(1, 2) };
}

double distance(const cv::Point2d& a, const cv::Point2d& b) { return std::hypot(a.x - b.x, a.y - b.y); }

Model::Circle unwarp(const cv::Matx33d& t, const Model::Circle& c) {
    const cv::Point2d p0 = apply(t, c.x, c.y), p1 = apply(t, c.x + c.diameter, c.y), p2 = apply(t, c.x, c.y + c.diameter);
    // Stretched or sheared, the diameter is the mean.
    return { p0.x, p0.y, (distance(p1, p0) + distance(p2, p0)) / 2.0 };
}

cv::RotatedRect unwarp(const cv::Matx33d& t, const cv::RotatedRect& r) {
    cv::Point2f pts[4];
    r.points(pts);
    cv::Point2d q[4];
    for (int i = 0; i < 4; ++i) q[i] = apply(t, pts[i].x, pts[i].y);
    const cv::Point2f center(float((q[0].x + q[2].x) / 2.0), float((q[0].y + q[2].y) / 2.0));
    const cv::Size2f size(float(distance(q[2], q[1])), float(distance(q[1], q[0])));
    // The angle from the longer side.
    const double angle = distance(q[0], q[1]) > distance(q[1], q[2]) ? std::atan2(q[1].y - q[0].y, q[1].x - q[0].x) * 180 / M_PI + 90
                                                                      : std::atan2(q[2].y - q[1].y, q[2].x - q[1].x) * 180 / M_PI;
    return cv::RotatedRect(center, size, float(angle));
}

Model::TemplateMatch unwarp(const cv::Matx33d& t, const Model::TemplateMatch& m) {
    const cv::Point2d p0 = apply(t, m.x, m.y), p1 = apply(t, m.x + m.width, m.y + m.height);
    // Its corner the least X and Y, its size positive (OpenPnP swaps them the
    // other way round, giving a negative size; a slip not kept).
    return { std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::abs(p1.x - p0.x), std::abs(p1.y - p0.y), m.score };
}

cv::KeyPoint unwarp(const cv::Matx33d& t, const cv::KeyPoint& k) {
    // A point a size away along its angle, taken back with it (OpenPnP takes
    // the arc cosine of the angle in degrees here, which is no number; the
    // angle's own cosine and sine are what it means).
    const double a = k.angle * M_PI / 180;
    const cv::Point2d p0 = apply(t, k.pt.x, k.pt.y), p1 = apply(t, k.pt.x + k.size * std::cos(a), k.pt.y - k.size * std::sin(a));
    return cv::KeyPoint(float(p0.x), float(p0.y), float(distance(p1, p0)), float(std::atan2(p0.y - p1.y, p1.x - p0.x) * 180 / M_PI));
}

} // namespace

void JPStageRegistry::addAffineStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "AffineWarp", "",
                      "Extracts a rectangular (parallelogrammatic) region of interest from the image that can have any position, size, rotation, scale, <em>shear</em> or even be mirrored. A so-called Affine Transformation Warp.<br/>The coordinates are given in real length units rather than pixels and are relative to the camera center, Y pointing up. This allows the pipeline to be independent of camera model and resolution, lens, focus distance etc. <br/>To setup, pin the previous stage and read off length unit coordinates from the mouse position.",
                      { P { "length-unit", Kind::Choice, "Millimeters", "Length unit used in this stage.",
                            { "Meters", "Centimeters", "Millimeters", "Feet", "Inches", "Mils", "Microns" } },
                        P { "x0", Kind::Number, "0.0", "What will become the left upper corner of the extracted area. X offset from camera location." },
                        P { "y0", Kind::Number, "0.0", "What will become the left upper corner of the extracted area. Y offset from camera location." },
                        P { "x1", Kind::Number, "0.0", "What will become the right upper corner of the extracted area. X offset from camera location." },
                        P { "y1", Kind::Number, "0.0", "What will become the right upper corner of the extracted area. Y offset from camera location." },
                        P { "x2", Kind::Number, "0.0", "What will become the left lower corner of the extracted area. X offset from camera location." },
                        P { "y2", Kind::Number, "0.0", "What will become the left lower corner of the extracted area. Y offset from camera location." },
                        P { "scale", Kind::Number, "1.0", "Scale of the transformation. NOTE: subsequent stages must be aware that the camera pixel to units scale has changed." },
                        P { "rectify", Kind::Flag, "true", "Rectify the transformation to be rectangular. The point [x2, y2] is not interpreted as a corner but as a height indicator." },
                        P { "region-of-interest-property", Kind::Text, "regionOfInterest", "The caller of the pipeline can override the region of interest under this name." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const auto& ctx = p.context();
                          if (ctx.pixelsPerMmX <= 0 || ctx.pixelsPerMmY <= 0) throw std::runtime_error("Property \"camera\" is required.");
                          const double unit = mmPer(s.text("length-unit"));
                          double x0 = s.number("x0"), y0 = s.number("y0"), x1 = s.number("x1"), y1 = s.number("y1"), x2 = s.number("x2"),
                                 y2 = s.number("y2");
                          bool rectify = s.flag("rectify");
                          const std::string roiName = s.text("region-of-interest-property");
                          if (!roiName.empty())
                              if (const JPPipelineValue* v = p.property(roiName))
                                  if (const auto* roi = std::get_if<JPPipelineValue::RegionOfInterest>(&v->value)) {
                                      // The caller's region, in the stage's unit.
                                      x0 = roi->upperLeft.x / unit, y0 = roi->upperLeft.y / unit;
                                      x1 = roi->upperRight.x / unit, y1 = roi->upperRight.y / unit;
                                      x2 = roi->lowerLeft.x / unit, y2 = roi->lowerLeft.y / unit;
                                      rectify = roi->rectify;
                                      for (const auto& [a, v2] : { std::pair { "x0", x0 }, { "y0", y0 }, { "x1", x1 }, { "y1", y1 }, { "x2", x2 }, { "y2", y2 } })
                                          p.noteOverride(s.name(), a, shown(v2));
                                      p.noteOverride(s.name(), "rectify", rectify ? "true" : "false");
                                  }
                          cv::Mat& mat = p.workingImage();
                          const double w = std::hypot(x1 - x0, y1 - y0);
                          double h = std::hypot(x2 - x0, y2 - y0);
                          if (w == 0.0 || h == 0.0) return Output {};
                          double x2eff = x2, y2eff = y2;
                          if (rectify) {
                              // The lower left square below the upper side, as far as it says.
                              const double nx = -(y1 - y0) / w, ny = (x1 - x0) / w;
                              h = (x2 - x0) * nx + (y2 - y0) * ny;
                              x2eff = x0 + h * nx;
                              y2eff = y0 + h * ny;
                          }
                          // Pixels a unit; the picture's centre as Java's integer halves.
                          const double sx = unit * ctx.pixelsPerMmX, sy = unit * ctx.pixelsPerMmY;
                          const double cx = double(mat.cols / 2), cy = double(mat.rows / 2);
                          const cv::Point2f p0(float(x0 * sx + cx), float(-y0 * sy + cy)), p1(float(x1 * sx + cx), float(-y1 * sy + cy)),
                              p2(float(x2eff * sx + cx), float(-y2eff * sy + cy));
                          const double scale = s.number("scale");
                          const double wPx = scale * w * sx, hPx = scale * std::abs(h) * sy;
                          if (wPx > std::hypot(mat.cols, mat.rows) + 2 || hPx > std::hypot(mat.cols, mat.rows) + 2) {
                              // Set up a corner at a time, the region can come out huge.
                              JLOGC(JPlacerLog::kPipeline, JLogLevel::Error)
                                  << "[org.openpnp.vision.pipeline.stages.AffineWarp] affineWarp must not generate an image that is larger than the original";
                              Output out;
                              out.model.value = std::string("ERROR: affineWarp must not generate an image that is larger than the original");
                              return out;
                          }
                          const cv::Point2f src[3] = { p0, p1, p2 };
                          const cv::Point2f dst[3] = { { 0, 0 }, { float(wPx), 0 }, { 0, float(hPx) } };
                          const cv::Mat m = cv::getAffineTransform(src, dst);
                          Output out;
                          cv::warpAffine(mat, out.image, m, cv::Size(int(wPx), int(hPx)));
                          // The picture-to-region transform, kept for AffineUnwarp.
                          const cv::Matx33d unit3(p1.x - p0.x, p2.x - p0.x, p0.x, p1.y - p0.y, p2.y - p0.y, p0.y, 0, 0, 1);
                          const cv::Matx33d unitT(wPx, 0, 0, 0, hPx, 0, 0, 0, 1);
                          cv::Matx33d at = cv::Matx33d::eye();
                          if (std::abs(cv::determinant(unit3)) > 0) at = unitT * unit3.inv();
                          else JLOGC(JPlacerLog::kPipeline, JLogLevel::Error)
                              << "[org.openpnp.vision.pipeline.stages.AffineWarp] getAffineTransform() cannot invert transformation";
                          out.model.value = cv::Matx23d(at(0, 0), at(0, 1), at(0, 2), at(1, 0), at(1, 1), at(1, 2));
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "AffineUnwarp", "",
                      "Result model coordinates obtained from images gone through AfficeWarp are not usable as camera coordinates. This stage applies the proper reverse Affine Transformation to reconstruct camera coordinates. The stage currently supports Lists or single instances of Circles, RotatedRects and KeyPoints. For transformations with stretch and shear, some of the model properties are approximated. ",
                      { P { "warp-stage-name", Kind::StageName, "", "Stage name of the AffineWarp." },
                        P { "results-stage-name", Kind::StageName, "", "Stage name of the results to unwarp. If empty, takes the working model." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string warp = s.text("warp-stage-name");
                          if (warp.empty()) return Output {};
                          const Model& warpModel = p.expectedResult(warp).model;
                          const auto* tx = std::get_if<cv::Matx23d>(&warpModel.value);
                          if (!tx) throw std::runtime_error("Stage \"" + warp + "\" returned a " + warpModel.kind() + " but expected a AffineTransform.");
                          const cv::Matx33d forward((*tx)(0, 0), (*tx)(0, 1), (*tx)(0, 2), (*tx)(1, 0), (*tx)(1, 1), (*tx)(1, 2), 0, 0, 1);
                          if (std::abs(cv::determinant(forward)) == 0) throw std::runtime_error("Determinant is 0");
                          const cv::Matx33d t = forward.inv();
                          const std::string from = s.text("results-stage-name");
                          const Model& model = from.empty() ? p.workingModel() : p.expectedResult(from).model;
                          Output out;
                          auto each = [&](const auto& list) {
                              auto copy = list;
                              for (auto& item : copy) item = unwarp(t, item);
                              if (!copy.empty()) out.model.value = std::move(copy);
                          };
                          if (const auto* v = std::get_if<std::vector<Model::Circle>>(&model.value)) each(*v);
                          else if (const auto* v = std::get_if<std::vector<cv::RotatedRect>>(&model.value)) each(*v);
                          else if (const auto* v = std::get_if<std::vector<Model::TemplateMatch>>(&model.value)) each(*v);
                          else if (const auto* v = std::get_if<std::vector<cv::KeyPoint>>(&model.value)) each(*v);
                          else if (const auto* c = std::get_if<Model::Circle>(&model.value)) out.model.value = unwarp(t, *c);
                          else if (const auto* r = std::get_if<cv::RotatedRect>(&model.value)) out.model.value = unwarp(t, *r);
                          else if (const auto* m = std::get_if<Model::TemplateMatch>(&model.value)) out.model.value = unwarp(t, *m);
                          else if (const auto* k = std::get_if<cv::KeyPoint>(&model.value)) out.model.value = unwarp(t, *k);
                          else throw std::runtime_error("Unsupported model type.");
                          return out;
                      } });
}

} // inline namespace jf
