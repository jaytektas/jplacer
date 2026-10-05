// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's stages that work on what earlier stages found: converted to
// points or key points, rotated rectangles turned upright or picked or
// filtered by size, the one closest to the centre, grown or shrunk, the
// picture masked by them or by shapes given, and a result composed.

#include "JPPipeline.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <regex>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
using Model = JPPipelineModel;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

std::vector<std::string> split(const std::string& text, const std::string& pattern) {
    std::vector<std::string> out;
    const std::regex re(pattern);
    std::sregex_token_iterator it(text.begin(), text.end(), re, -1), end;
    for (; it != end; ++it) out.push_back(*it);
    return out;
}

// A model's rotated rectangles, one or a list.
std::vector<cv::RotatedRect> rectsOf(const Model& m, bool& single) {
    single = false;
    if (const auto* one = std::get_if<cv::RotatedRect>(&m.value)) {
        single = true;
        return { *one };
    }
    if (const auto* list = std::get_if<std::vector<cv::RotatedRect>>(&m.value)) return *list;
    throw std::runtime_error("Pipeline stage returned a " + m.kind() + " but expected a RotatedRect list.");
}

cv::Mat colorMask(const cv::Mat& mat, const JPPipelineStage& s) {
    cv::Mat mask(mat.size(), mat.type());
    mask.setTo(s.hasColor("color") ? s.color("color") : JPStageUtil::indexedColor(0));
    return mask;
}

const cv::Scalar kWhite(255, 255, 255);

void fillRect(cv::Mat& mask, const cv::RotatedRect& r) {
    cv::Point2f p[4];
    r.points(p);
    std::vector<std::vector<cv::Point>> poly { { p[0], p[1], p[2], p[3] } };
    cv::fillPoly(mask, poly, kWhite);
}

} // namespace

void JPStageRegistry::addModelStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "ConvertModelToPoints", "",
                      "Convert a variety of built in types to Points. Currently handles KeyPoints, Circles and RotatedRects. The center point of each is stored. If the input model is a single value the result will be a single value. If the input is a List the result will be a List.",
                      { P { "model-stage-name", Kind::StageName, "", "" } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("model-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const Model& m = p.expectedResult(from).model;
                          Output out;
                          auto bad = [&m] { return std::runtime_error("Don't know how to convert " + m.describe() + "to Point."); };
                          if (const auto* c = std::get_if<std::vector<Model::Circle>>(&m.value)) {
                              std::vector<cv::Point2d> pts;
                              for (const auto& x : *c) pts.emplace_back(x.x, x.y);
                              out.model.value = pts;
                          } else if (const auto* k = std::get_if<std::vector<cv::KeyPoint>>(&m.value)) {
                              std::vector<cv::Point2d> pts;
                              for (const auto& x : *k) pts.emplace_back(x.pt);
                              out.model.value = pts;
                          } else if (const auto* r = std::get_if<std::vector<cv::RotatedRect>>(&m.value)) {
                              std::vector<cv::Point2d> pts;
                              for (const auto& x : *r) pts.emplace_back(x.center);
                              out.model.value = pts;
                          } else if (const auto* c1 = std::get_if<Model::Circle>(&m.value)) {
                              out.model.value = cv::Point2d(c1->x, c1->y);
                          } else if (const auto* k1 = std::get_if<cv::KeyPoint>(&m.value)) {
                              out.model.value = cv::Point2d(k1->pt);
                          } else if (const auto* r1 = std::get_if<cv::RotatedRect>(&m.value)) {
                              out.model.value = cv::Point2d(r1->center);
                          } else if (!m.empty()) {
                              throw bad();
                          }
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "ConvertModelToKeyPoints", "",
                      "Convert a variety of built in types to KeyPoints. Currently handles Points, Circles, TemplateMatches, and RotatedRects. The center point of each is stored, along with a score and diameter where appropriate. If the input model is a single value the result will be a single value. If the input is a List the result will be a List.",
                      { P { "model-stage-name", Kind::StageName, "", "" } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("model-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const Model& m = p.expectedResult(from).model;
                          auto circle = [](const Model::Circle& c) { return cv::KeyPoint(float(c.x), float(c.y), float(c.diameter)); };
                          auto point = [](const cv::Point2d& pt) { return cv::KeyPoint(float(pt.x), float(pt.y), 0); };
                          auto rect = [](const cv::RotatedRect& r) { return cv::KeyPoint(r.center.x, r.center.y, 0, r.angle); };
                          auto match = [](const Model::TemplateMatch& t) {
                              const float w = float(t.width), h = float(t.height);
                              return cv::KeyPoint(float(t.x) + w / 2, float(t.y) + h / 2, w + h, float(t.score));
                          };
                          Output out;
                          std::vector<cv::KeyPoint> list;
                          if (const auto* c = std::get_if<std::vector<Model::Circle>>(&m.value)) for (const auto& x : *c) list.push_back(circle(x));
                          else if (const auto* pts = std::get_if<std::vector<cv::Point2d>>(&m.value)) for (const auto& x : *pts) list.push_back(point(x));
                          else if (const auto* r = std::get_if<std::vector<cv::RotatedRect>>(&m.value)) for (const auto& x : *r) list.push_back(rect(x));
                          else if (const auto* t = std::get_if<std::vector<Model::TemplateMatch>>(&m.value)) for (const auto& x : *t) list.push_back(match(x));
                          else if (const auto* k = std::get_if<std::vector<cv::KeyPoint>>(&m.value)) list = *k;
                          else if (const auto* c1 = std::get_if<Model::Circle>(&m.value)) { out.model.value = circle(*c1); return out; }
                          else if (const auto* p1 = std::get_if<cv::Point2d>(&m.value)) { out.model.value = point(*p1); return out; }
                          else if (const auto* r1 = std::get_if<cv::RotatedRect>(&m.value)) { out.model.value = rect(*r1); return out; }
                          else if (m.empty()) return out;
                          else throw std::runtime_error("Don't know how to convert " + m.describe() + "to KeyPoint.");
                          out.model.value = list;
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "OrientRotatedRects", "Model Transforms",
                      "Sets the angle of a RotatedRect or List<RotatedRect> to match the specified orientation, either landscape or portrait.",
                      { P { "rotated-rects-stage-name", Kind::StageName, "", "Name of a prior stage containing a RotatedRect or List<RotatedRect>." },
                        P { "orientation", Kind::Choice, "Landscape", "", { "Landscape", "Portrait", "SnapToAngle" } },
                        P { "negate-angle", Kind::Flag, "false", "" },
                        P { "snap-angle", Kind::Integer, "0", "Expected angle during perfect pick operation" } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("rotated-rects-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const Model& m = p.expectedResult(from).model;
                          if (m.empty()) return Output {};
                          const std::string orientation = s.text("orientation");
                          const bool negate = s.flag("negate-angle");
                          const int snap = s.integer("snap-angle");
                          auto orient = [&](cv::RotatedRect r) {
                              if ((r.size.height > r.size.width && orientation == "Landscape")
                                  || (r.size.width > r.size.height && orientation == "Portrait")) {
                                  std::swap(r.size.width, r.size.height);
                                  r.angle -= 90;
                              }
                              if (orientation == "SnapToAngle") {
                                  double a = std::fmod(r.angle + 360, 90.0);
                                  if (a > 45) a -= 90;
                                  else std::swap(r.size.width, r.size.height);
                                  r.angle = float(a + snap);
                              }
                              if (negate) r.angle = -r.angle;
                              return r;
                          };
                          Output out;
                          if (const auto* one = std::get_if<cv::RotatedRect>(&m.value)) {
                              out.model.value = orient(*one);
                          } else if (const auto* list = std::get_if<std::vector<cv::RotatedRect>>(&m.value)) {
                              std::vector<cv::RotatedRect> rects;
                              for (const auto& r : *list) rects.push_back(orient(r));
                              out.model.value = rects;
                          }
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "SelectSingleRect", "Image Processing",
                      "Selects a single rotated rectangle from a list. Main purpose convert the data type.",
                      { P { "position", Kind::Integer, "0", "Position (0=first, 1=second, -1=last)" },
                        P { "rotated-rects-stage-name", Kind::StageName, "", "Previous pipeline stage that outputs rotated rects." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("rotated-rects-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const Model& m = p.expectedResult(from).model;
                          if (m.empty()) return Output {};
                          bool single = false;
                          const std::vector<cv::RotatedRect> rects = rectsOf(m, single);
                          const int position = s.integer("position"), n = int(rects.size());
                          // From the end when negative (-1 the last).
                          const int at = position < 0 ? n + position : position;
                          Output out;
                          if (at >= 0 && at < n) out.model.value = rects[size_t(at)];
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "FilterRects", "Image Processing", "Filter rotated rects based on given width, length and aspect ratio limits.",
                      { P { "width-max", Kind::Number, "50.0", "Max width of filtered rects." },
                        P { "width-min", Kind::Number, "25.0", "Min width of filtered rects." },
                        P { "length-max", Kind::Number, "50.0", "Max length of filtered rects." },
                        P { "length-min", Kind::Number, "25.0", "Min length of filtered rects." },
                        P { "aspect-ratio-max", Kind::Number, "0.0", "Max aspect ratio for selecting rects, used if one or both of width and length are 0. If both width and length are 0, then any rect with size satisfying the aspect ratio limits will pass through." },
                        P { "aspect-ratio-min", Kind::Number, "0.0", "Min aspect ratio for selecting rects, used if one or both of width and length are 0. If both width and length are 0, then any rect with size satisfying the aspect ratio limits will pass through." },
                        P { "enable-logging", Kind::Flag, "false", "Enable logging of rect data." },
                        P { "rotated-rects-stage-name", Kind::StageName, "", "Previous pipeline stage that outputs rotated rects." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("rotated-rects-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const Model& m = p.expectedResult(from).model;
                          if (m.empty()) return Output {};
                          bool single = false;
                          const std::vector<cv::RotatedRect> rects = rectsOf(m, single);
                          auto num = [&s](const char* a) { return std::abs(s.number(a)); };
                          double wmax = std::max(num("width-max"), num("width-min")), wmin = std::min(num("width-max"), num("width-min"));
                          double lmax = std::max(num("length-max"), num("length-min")), lmin = std::min(num("length-max"), num("length-min"));
                          const double armax = std::max(num("aspect-ratio-max"), num("aspect-ratio-min"));
                          const double armin = std::min(num("aspect-ratio-max"), num("aspect-ratio-min"));
                          if (armin == 0 && (wmax == 0 || lmax == 0))
                              throw std::runtime_error("If width or length limits are 0, then both aspectRatioMax and aspepctRatioMin must be non zero.");
                          int sizeType;
                          if (armax != 0 && wmax == 0 && lmax == 0) sizeType = 2;
                          else if (lmax == 0) {
                              lmax = wmax / armin;
                              lmin = wmin / armax;
                              sizeType = 1;
                          } else if (wmax == 0) {
                              wmax = lmax * armax;
                              wmin = lmin * armin;
                              sizeType = 1;
                          } else {
                              sizeType = 3;
                          }
                          const bool logging = s.flag("enable-logging");
                          std::vector<cv::RotatedRect> kept;
                          for (const cv::RotatedRect& r : rects) {
                              const double rw = std::max(r.size.width, r.size.height), rl = std::min(r.size.width, r.size.height);
                              const double rar = rw / rl;
                              if (std::isnan(rar) || std::isinf(rar)) continue;
                              if (sizeType == 2) {
                                  lmin = lmax = rl;
                                  wmax = rl * armax;
                                  wmin = rl * armin;
                              }
                              const bool wok = rw >= wmin && rw <= wmax, lok = rl >= lmin && rl <= lmax, arok = rar >= armin && rar <= armax;
                              const bool pass = wok && lok && (sizeType == 3 || arok);
                              if (pass) kept.push_back(r);
                              if (logging)
                                  JLOGC(JPlacerLog::kPipeline, JLogLevel::Info)
                                      << (pass ? "+" : " ") << " rect: xy=" << int(r.center.x) << ", " << int(r.center.y)
                                      << " area=" << int(rw * rl) << " angle= " << int(r.angle) << "°";
                          }
                          if (logging) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "Total detected rects: " << kept.size();
                          Output out;
                          if (kept.empty()) return out;
                          if (single) out.model.value = kept.front();
                          else out.model.value = kept;
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "ClosestModel", "Image Processing",
                      "Filter RotatedRects that fit in the template's size, with a tolerance and find the closest RotatedRect to the center of the screen.",
                      { P { "log", Kind::Flag, "false", "Allow log messages in the log." },
                        P { "filter-stage-name", Kind::StageName, "", "Name of a prior stage to load the filter size from. The filter stage should contain a single RotatedRect model." },
                        P { "model-stage-name", Kind::StageName, "", "Name of a prior stage to load the model from." },
                        P { "tolerance", Kind::Number, "0.2", "Filter tolerance." },
                        P { "scale", Kind::Number, "1.0", "Scale filter by this value." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("model-stage-name"), filter = s.text("filter-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const Model& m = p.expectedResult(from).model;
                          const cv::Mat& image = p.workingImage();
                          if (m.empty()) return Output {};
                          std::optional<cv::RotatedRect> frect;
                          if (!JPStageUtil::blank(filter)) {
                              const Model& f = p.expectedResult(filter).model;
                              if (const auto* one = std::get_if<cv::RotatedRect>(&f.value)) frect = *one;
                              else if (const auto* list = std::get_if<std::vector<cv::RotatedRect>>(&f.value); list && !list->empty())
                                  frect = list->front();
                              else return Output {};
                          }
                          const double tol = s.number("tolerance"), scale = s.number("scale");
                          double hMax = 0, hMin = 0, wMax = 0, wMin = 0;
                          if (frect) {
                              const double big = std::max(frect->size.width, frect->size.height), small = std::min(frect->size.width, frect->size.height);
                              hMax = big * (1 + tol / 2) * scale;
                              hMin = big * (1 - tol / 2) * scale;
                              wMax = small * (1 + tol / 2) * scale;
                              wMin = small * (1 - tol / 2) * scale;
                          }
                          std::vector<cv::RotatedRect> candidates;
                          if (const auto* one = std::get_if<cv::RotatedRect>(&m.value)) candidates = { *one };
                          else if (const auto* list = std::get_if<std::vector<cv::RotatedRect>>(&m.value); list && !list->empty())
                              candidates = *list;
                          else return Output {};
                          const cv::Point2d centre(image.cols / 2.0, image.rows / 2.0);
                          double best = 10e8;
                          std::optional<cv::RotatedRect> closest;
                          for (const cv::RotatedRect& r : candidates) {
                              const double h = std::max(r.size.width, r.size.height), w = std::min(r.size.width, r.size.height);
                              if (frect && (h > hMax || h < hMin || w > wMax || w < wMin)) continue;
                              const double d = std::hypot(centre.x - r.center.x, centre.y - r.center.y);
                              if (d < best) {
                                  best = d;
                                  closest = r;
                              }
                          }
                          Output out;
                          if (closest) out.model.value = std::vector<cv::RotatedRect> { *closest };
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "DilateModel", "Image Processing", "Dilate or contract a model by a given number of pixels.",
                      { P { "model-stage-name", Kind::StageName, "", "Name of stage to input model data from." },
                        P { "dilate", Kind::Integer, "0", "Dilate the model by given pixels. Negative values contract the model." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("model-stage-name");
                          if (JPStageUtil::blank(from)) throw std::runtime_error("Stage name for model must be specified.");
                          const Model& m = p.expectedResult(from).model;
                          const float d = float(s.integer("dilate") * 2);
                          auto grow = [d](cv::RotatedRect r) {
                              r.size.width += d;
                              r.size.height += d;
                              return r;
                          };
                          Output out;
                          out.image = p.workingImage();
                          if (const auto* r = std::get_if<cv::RotatedRect>(&m.value)) out.model.value = grow(*r);
                          else if (const auto* c = std::get_if<Model::Circle>(&m.value)) out.model.value = Model::Circle { c->x, c->y, c->diameter + d };
                          else if (const auto* cs = std::get_if<std::vector<Model::Circle>>(&m.value)) {
                              std::vector<Model::Circle> list;
                              for (const auto& c1 : *cs) list.push_back({ c1.x, c1.y, c1.diameter + d });
                              out.model.value = list;
                          } else if (const auto* rs = std::get_if<std::vector<cv::RotatedRect>>(&m.value)) {
                              std::vector<cv::RotatedRect> list;
                              for (const auto& r1 : *rs) list.push_back(grow(r1));
                              out.model.value = list;
                          } else {
                              out.model = m;
                          }
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "MaskModel", "Image Processing", "Mask an image with model shapes originating from previous stages.",
                      { P { "color", Kind::Color, "0,0,0,255", "Color of mask." },
                        P { "model-stage-name", Kind::StageName, "", "Name of stage to input model data from." },
                        P { "is-mask", Kind::Flag, "false", "Filter or mask the image." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("model-stage-name");
                          if (JPStageUtil::blank(from)) throw std::runtime_error("Stage name for model must be specified.");
                          const cv::Mat& mat = p.workingImage();
                          cv::Mat mask = colorMask(mat, s);
                          cv::Mat masked = mask.clone();
                          const Model& m = p.expectedResult(from).model;
                          if (const auto* r = std::get_if<cv::RotatedRect>(&m.value)) fillRect(mask, *r);
                          else if (const auto* c = std::get_if<Model::Circle>(&m.value))
                              cv::circle(mask, cv::Point2d(c->x, c->y), int(c->diameter) / 2, kWhite, -1);
                          else if (const auto* cs = std::get_if<std::vector<Model::Circle>>(&m.value))
                              for (const auto& c1 : *cs) cv::circle(mask, cv::Point2d(c1.x, c1.y), int(c1.diameter) / 2, kWhite, -1);
                          else if (const auto* rs = std::get_if<std::vector<cv::RotatedRect>>(&m.value))
                              for (const auto& r1 : *rs) fillRect(mask, r1);
                          else if (const auto* contours = std::get_if<Model::Contours>(&m.value))
                              for (int i = 0; i < int(contours->size()); ++i) cv::drawContours(mask, *contours, i, kWhite, -1);
                          if (s.flag("is-mask")) cv::bitwise_not(mask, mask);
                          mat.copyTo(masked, mask);
                          Output out;
                          out.image = masked;
                          out.model = m;
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "MaskPolygon", "Image Processing",
                      "Mask an image with multiple shapes formed with numeric data provided by the user.",
                      { P { "color", Kind::Color, "0,0,0,255", "Color of mask." },
                        P { "shapes", Kind::Text, "", "Coordinates forming shapes. X,Y coordinates or sizes are separated by commas, coordinate or size pairs are separated by colons ':'. Multiple shapes are separated by semicolons ';'" },
                        P { "inverted", Kind::Flag, "false", "Invert the mask." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string shapes = s.text("shapes");
                          if (shapes.empty()) throw std::runtime_error("Mask shape coordinates must be specified.");
                          const cv::Mat& mat = p.workingImage();
                          cv::Mat mask = colorMask(mat, s);
                          cv::Mat masked = mask.clone();
                          for (const std::string& item : split(shapes, R"(\s*;\s*)")) {
                              const std::vector<std::string> atoms = split(item, R"(\s*:\s*)");
                              try {
                                  if (atoms.size() == 2) {
                                      // A circle (x, y: radius) or a rectangle (x, y: width, height), centred.
                                      const std::vector<std::string> c = split(item, R"(\s*(,|:)\s*)");
                                      if (c.size() == 3) {
                                          cv::circle(mask, cv::Point(std::stoi(c[0]), std::stoi(c[1])), std::stoi(c[2]), kWhite, -1);
                                      } else if (c.size() >= 4) {
                                          const int x = std::stoi(c[0]), y = std::stoi(c[1]), w = std::stoi(c[2]), h = std::stoi(c[3]);
                                          cv::rectangle(mask, cv::Point(x - w / 2, y + h / 2), cv::Point(x + w / 2, y - h / 2), kWhite, -1);
                                      }
                                  } else if (atoms.size() > 2) {
                                      std::vector<std::vector<cv::Point>> poly(1);
                                      for (const std::string& a : atoms) {
                                          const std::vector<std::string> c = split(a, R"(\s*,\s*)");
                                          poly[0].emplace_back(std::stoi(c.at(0)), std::stoi(c.at(1)));
                                      }
                                      cv::fillPoly(mask, poly, kWhite);
                                  }
                              } catch (const std::exception& e) {
                                  JLOGC(JPlacerLog::kPipeline, JLogLevel::Error) << "Cannot parse number. " << e.what();
                              }
                          }
                          if (s.flag("inverted")) cv::bitwise_not(mask, mask);
                          mat.copyTo(masked, mask);
                          Output out;
                          out.image = masked;
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "ComposeResult", "Image Processing",
                      "Retrieves the result of a user defined stage, or the working result in the pipeline, when this stage is processed.",
                      { P { "image-stage-name", Kind::StageName, "", "Name of a prior stage to retrieve the image from. An empty name will retrieve the working image." },
                        P { "model-stage-name", Kind::StageName, "", "Name of a prior stage to retrieve the model from." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string imageFrom = s.text("image-stage-name"), modelFrom = s.text("model-stage-name");
                          Output out;
                          if (!JPStageUtil::blank(modelFrom)) out.model = p.expectedResult(modelFrom).model;
                          if (JPStageUtil::blank(imageFrom)) {
                              out.image = p.workingImage();
                              out.colorSpace = p.workingColorSpace();
                          } else {
                              const JPPipeline::Result& r = p.expectedResult(imageFrom);
                              out.image = r.image;
                              out.colorSpace = r.colorSpace;
                          }
                          return out;
                      } });
}

} // inline namespace jf
