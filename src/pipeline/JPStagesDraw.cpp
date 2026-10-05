// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's stages that draw what earlier stages found over the working
// image: contours, rotated rectangles, ellipses, circles, key points,
// template matches, and a mark at the picture's centre. A colour not set:
// each item its own (FluentCv.indexedColor).

#include "JPPipeline.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"

#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
using Model = JPPipelineModel;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

cv::Scalar colorOr(const JPPipelineStage& s, const char* element, int i) {
    return s.hasColor(element) ? s.color(element) : JPStageUtil::indexedColor(i);
}

// A stage's rotated rectangles: one, or a list.
std::vector<cv::RotatedRect> rectsOf(const JPPipeline::Result& r) {
    if (const auto* one = std::get_if<cv::RotatedRect>(&r.model.value)) return { *one };
    if (const auto* list = std::get_if<std::vector<cv::RotatedRect>>(&r.model.value)) return *list;
    return {};
}

void drawRotatedRect(cv::Mat& mat, const cv::RotatedRect& r, const cv::Scalar& color, int thickness) {
    cv::Point2f pts[4];
    r.points(pts);
    for (int j = 0; j < 4; ++j) cv::line(mat, pts[j], pts[(j + 1) % 4], color, thickness, cv::LINE_AA);
}

} // namespace

void JPStageRegistry::addDrawStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "DrawContours", "", "",
                      { P { "color", Kind::Color, "", "" }, P { "contours-stage-name", Kind::StageName, "", "" },
                        P { "thickness", Kind::Integer, "1", "" },
                        P { "index", Kind::Integer, "-1", "The index of the contour in the list to draw. Any negative value will draw all contours." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("contours-stage-name");
                          if (JPStageUtil::blank(from)) throw std::runtime_error("contoursStageName is required.");
                          const JPPipeline::Result& r = p.expectedResult(from);
                          if (r.model.empty()) return Output {};
                          const auto* contours = std::get_if<Model::Contours>(&r.model.value);
                          if (!contours)
                              throw std::runtime_error("Pipeline stage \"" + from + "\" returned a " + r.model.kind() +
                                                       " but expected a MatOfPoint list.");
                          cv::Mat& mat = p.workingImage();
                          const int index = s.integer("index"), thickness = s.integer("thickness");
                          if (index < 0) {
                              for (int i = 0; i < int(contours->size()); ++i)
                                  cv::drawContours(mat, *contours, i, colorOr(s, "color", i), thickness);
                          } else {
                              cv::drawContours(mat, *contours, index, colorOr(s, "color", index), thickness);
                          }
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "DrawRotatedRects", "Image Processing",
                      "Draws RotatedRects from a stage's model. Input can be either a single RotatedRect or a List of RotatedRect",
                      { P { "color", Kind::Color, "", "" },
                        P { "rotated-rects-stage-name", Kind::StageName, "", "Stage to input RotatedRect from." },
                        P { "thickness", Kind::Integer, "1", "Thickness of RotatedRect outline." },
                        P { "draw-rect-center", Kind::Flag, "false", "Draw a circle at the center of each RotatedRect." },
                        P { "rect-center-radius", Kind::Integer, "20", "Radius of circle at center of RotatedRects." },
                        P { "show-orientation", Kind::Flag, "false", "Show the orientation of a rotated rect." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("rotated-rects-stage-name");
                          if (JPStageUtil::blank(from)) throw std::runtime_error("rotatedRectsStageName must be specified.");
                          const JPPipeline::Result& r = p.expectedResult(from);
                          if (r.model.empty()) return Output {};
                          cv::Mat& mat = p.workingImage();
                          const std::vector<cv::RotatedRect> rects = rectsOf(r);
                          const int thickness = s.integer("thickness");
                          for (int i = 0; i < int(rects.size()); ++i) {
                              const cv::RotatedRect& rect = rects[size_t(i)];
                              const cv::Scalar c = colorOr(s, "color", i);
                              drawRotatedRect(mat, rect, c, thickness);
                              if (s.flag("draw-rect-center"))
                                  cv::circle(mat, rect.center, s.integer("rect-center-radius"), c, thickness, cv::LINE_AA);
                              if (s.flag("show-orientation")) {
                                  const double mark = (rect.angle - 90.0) * CV_PI / 180;
                                  cv::line(mat, rect.center,
                                           cv::Point2d(rect.center.x + 1.2 * rect.size.height / 2.0 * std::cos(mark),
                                                       rect.center.y + 1.2 * rect.size.height / 2.0 * std::sin(mark)),
                                           c, std::abs(thickness), cv::LINE_AA);
                              }
                          }
                          Output out;
                          out.model.value = rects;
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "DrawEllipses", "Image Processing",
                      "Draws RotatedRects from a stage's model. Input can be either a single RotatedRect or a List of RotatedRect",
                      { P { "color", Kind::Color, "", "" },
                        P { "ellipses-stage-name", Kind::StageName, "", "Stage to input FitEllipse RotatedRects from." },
                        P { "thickness", Kind::Integer, "1", "Thickness of Ellipse outline." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("ellipses-stage-name");
                          if (JPStageUtil::blank(from)) throw std::runtime_error("ellipsesStageName must be specified.");
                          const JPPipeline::Result& r = p.expectedResult(from);
                          if (r.model.empty()) return Output {};
                          cv::Mat& mat = p.workingImage();
                          const std::vector<cv::RotatedRect> rects = rectsOf(r);
                          for (int i = 0; i < int(rects.size()); ++i) {
                              const cv::RotatedRect& rect = rects[size_t(i)];
                              cv::ellipse(mat, rect.center, cv::Size2d(rect.size.width * 0.5, rect.size.height * 0.5), rect.angle, 0,
                                          360, colorOr(s, "color", i), s.integer("thickness"), cv::LINE_AA);
                          }
                          Output out;
                          out.model.value = rects;
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "DrawCircles", "", "",
                      { P { "color", Kind::Color, "", "" }, P { "center-color", Kind::Color, "", "" },
                        P { "circles-stage-name", Kind::StageName, "", "" }, P { "thickness", Kind::Integer, "1", "" } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("circles-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const JPPipeline::Result& r = p.expectedResult(from);
                          if (r.model.empty()) return Output {};
                          const auto* circles = std::get_if<std::vector<Model::Circle>>(&r.model.value);
                          if (!circles)
                              throw std::runtime_error("Pipeline stage \"" + from + "\" returned a " + r.model.kind() +
                                                       " but expected a Circle list.");
                          cv::Mat& mat = p.workingImage();
                          for (int i = 0; i < int(circles->size()); ++i) {
                              const Model::Circle& c = (*circles)[size_t(i)];
                              const cv::Scalar color = colorOr(s, "color", i);
                              const cv::Scalar center = s.hasColor("center-color") ? s.color("center-color") : JPStageUtil::complementary(color);
                              cv::circle(mat, cv::Point2d(c.x, c.y), int(c.diameter / 2), color, s.integer("thickness"), cv::LINE_AA);
                              cv::circle(mat, cv::Point2d(c.x, c.y), 1, center, 2, cv::LINE_AA);
                          }
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "DrawKeyPoints", "",
                      "Draws KeyPoints contained in a List<KeyPoint> by referencing a previous stage's model data.",
                      { P { "color", Kind::Color, "", "" }, P { "key-points-stage-name", Kind::StageName, "", "" } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("key-points-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const JPPipeline::Result& r = p.expectedResult(from);
                          const auto* points = std::get_if<std::vector<cv::KeyPoint>>(&r.model.value);
                          if (!points) return Output {};
                          cv::Mat& mat = p.workingImage();
                          if (s.hasColor("color")) cv::drawKeypoints(mat, *points, mat, s.color("color"));
                          else cv::drawKeypoints(mat, *points, mat);
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "DrawTemplateMatches", "", "",
                      { P { "color", Kind::Color, "", "" }, P { "template-matches-stage-name", Kind::StageName, "", "" } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("template-matches-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const JPPipeline::Result& r = p.expectedResult(from);
                          if (r.model.empty()) return Output {};
                          const auto* matches = std::get_if<std::vector<Model::TemplateMatch>>(&r.model.value);
                          if (!matches)
                              throw std::runtime_error("Pipeline stage \"" + from + "\" returned a " + r.model.kind() +
                                                       " but expected a TemplateMatch list.");
                          cv::Mat& mat = p.workingImage();
                          for (int i = 0; i < int(matches->size()); ++i) {
                              const Model::TemplateMatch& m = (*matches)[size_t(i)];
                              const cv::Scalar c = colorOr(s, "color", i);
                              cv::rectangle(mat, cv::Point2d(m.x, m.y), cv::Point2d(m.x + m.width, m.y + m.height), c);
                              char score[32];
                              std::snprintf(score, sizeof score, "%.3f", m.score);
                              cv::putText(mat, score, cv::Point2d(m.x + m.width, m.y + m.height), cv::FONT_HERSHEY_PLAIN, 1.0, c);
                          }
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "DrawImageCenter", "Image Processing", "Draw a mark at the center of the image.",
                      { P { "show-image-center", Kind::Flag, "true", "Show a mark at the center of the image." },
                        P { "color", Kind::Color, "", "Color for the center mark." },
                        P { "thickness", Kind::Integer, "2", "Thickness of center mark." },
                        P { "size", Kind::Integer, "40", "Size of center mark." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          cv::Mat& mat = p.workingImage();
                          if (s.flag("show-image-center")) {
                              const int cx = mat.cols / 2, cy = mat.rows / 2, size = s.integer("size"), t = s.integer("thickness");
                              const cv::Scalar c = colorOr(s, "color", 0);
                              cv::line(mat, cv::Point(cx - size / 2, cy), cv::Point(cx + size / 2, cy), c, t);
                              cv::line(mat, cv::Point(cx, cy - size / 2), cv::Point(cx, cy + size / 2), c, t);
                          }
                          Output out;
                          out.image = mat;
                          return out;
                      } });
}

} // inline namespace jf
