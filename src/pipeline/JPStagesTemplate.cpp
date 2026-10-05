// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's stages that make a template and find a part by it: one drawn
// from a shape or a footprint, cut from a model, read from or written to the
// part's template file; and a found part turned the way its template matches
// best, a quarter turn at a time.

#include "JPPipeline.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
using Model = JPPipelineModel;
using Value = JPPipelineValue;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

const Value::Part* partOf(const JPPipeline& p) {
    const Value* v = p.property("part");
    return v ? std::get_if<Value::Part>(&v->value) : nullptr;
}

// A rotated rectangle from a stage's model: it, or the first of a list.
std::optional<cv::RotatedRect> rectOf(const Model& m) {
    if (const auto* r = std::get_if<cv::RotatedRect>(&m.value)) return *r;
    if (const auto* l = std::get_if<std::vector<cv::RotatedRect>>(&m.value); l && !l->empty()) return l->front();
    return std::nullopt;
}

// Upright: the long side along Y (a quarter turn added when it was along X).
void portrait(cv::RotatedRect& r) {
    r.angle += 90;
    std::swap(r.size.width, r.size.height);
}

// OpenPnP's rotateRect: the picture turned about the rectangle's centre and
// cut to the turned rectangle's bounds; the rectangle turned with it.
cv::Mat rotateRect(const cv::Mat& mat, cv::RotatedRect& rect, double degrees) {
    cv::Mat map = cv::getRotationMatrix2D(rect.center, degrees, 1.0);
    rect.angle -= float(degrees);
    cv::Rect box = rect.boundingRect();
    map.at<double>(0, 2) += box.width / 2.0 - rect.center.x;
    map.at<double>(1, 2) += box.height / 2.0 - rect.center.y;
    cv::Mat out;
    cv::warpAffine(mat, out, map, box.size(), cv::INTER_LINEAR);
    box = rect.boundingRect();
    rect.center = { float(box.width / 2.0), float(box.height / 2.0) };
    return out;
}

// The template's best match score in the picture, at or above `threshold`
// (0: none).
double bestMatch(const cv::Mat& image, const cv::Mat& templ, double threshold) {
    if (templ.cols > image.cols || templ.rows > image.rows) return 0;
    cv::Mat result;
    cv::matchTemplate(image, templ, result, cv::TM_CCOEFF_NORMED);
    double maxVal = 0;
    cv::minMaxLoc(result, nullptr, &maxVal);
    double best = 0;
    for (const cv::Point& pt : JPStageUtil::matMaxima(result, threshold, maxVal)) best = std::max(best, double(result.at<float>(pt.y, pt.x)));
    return best;
}

// OpenPnP's MatchPart(s)Template for one part: the picture cut around it
// (square, its diagonal across), the template turned to it, then matched at
// each quarter turn; the part's angle is the best turn's (none matched: no
// part). `asTemplate`: put the part long side as the template's, else upright.
std::optional<cv::RotatedRect> turnedByTemplate(const cv::Mat& original, const JPPipeline::Result& templ, cv::RotatedRect rrect,
                                                double threshold, bool asTemplate, bool log) {
    cv::Mat timage = templ.image.clone();
    cv::RotatedRect trect = std::get_if<cv::RotatedRect>(&templ.model.value)
                                ? *std::get_if<cv::RotatedRect>(&templ.model.value)
                                : cv::RotatedRect(cv::Point2f(float(timage.cols / 2), float(timage.rows / 2)), cv::Size2f(timage.size()), 0);
    if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "part found = " << rrect.center.x << ", " << rrect.center.y << " "
                                                               << rrect.size.width << "x" << rrect.size.height << " " << rrect.angle;
    if (asTemplate) {
        const bool templateWide = trect.size.width / trect.size.height > 1.0;
        if ((rrect.size.width > rrect.size.height && !templateWide) || (rrect.size.width < rrect.size.height && templateWide))
            portrait(rrect);
    } else if (rrect.size.width > rrect.size.height) {
        portrait(rrect);
    }
    cv::RotatedRect orect = rrect;
    const cv::Rect box = rrect.boundingRect();
    const double msz = std::hypot(box.width, box.height);
    cv::Mat image;
    cv::getRectSubPix(original, cv::Size(int(msz), int(msz)), rrect.center, image);
    rrect.center = { float(msz / 2), float(msz / 2) };
    timage = rotateRect(timage, trect, -rrect.angle);
    double maxScore = 0;
    int winner = 0;
    for (int i = 1; i <= 4; ++i) {
        if (i > 1) {
            // A quarter turn clockwise.
            cv::Mat t = timage.t();
            cv::flip(t, timage, 1);
        }
        const double score = bestMatch(image, timage, threshold);
        if (score > maxScore) {
            maxScore = score;
            winner = i;
        }
        if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "rotation" << i << " score = " << score;
    }
    if (winner == 0) {
        JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "NO MATCH FOUND!!!!!!!";
        return std::nullopt;
    }
    orect.angle = float(std::fmod(rrect.angle + (winner - 1) * 90.0, 360.0));
    if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "winning rotation = " << winner;
    return orect;
}

// The default templates directory, or the stage's own setting.
std::string templatePath(const JPPipeline& p, const std::string& set, const char* under) {
    if (!JPStageUtil::blank(set)) return set;
    return p.context().configurationDirectory + "/templates" + under;
}

bool endsWith(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

} // namespace

void JPStageRegistry::addTemplateStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "CreateShapeTemplateImage", "",
                      "Creates a template from the specified shape and camera properties. The shape is scaled from Millimeters to the camera's units.",
                      { P { "template-shape-name", Kind::Text, "", "Name of the shape property to load the template shape from. The shape itself must be provided by the pipeline user." },
                        P { "oversize", Kind::Number, "1.5", "Oversize factor for border recognition around shape." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string name = s.text("template-shape-name");
                          if (JPStageUtil::blank(name)) return Output {};
                          const auto& ctx = p.context();
                          if (ctx.pixelsPerMmX <= 0 || ctx.pixelsPerMmY <= 0) throw std::runtime_error("Property \"camera\" is required.");
                          const Value* v = p.property(name);
                          const auto* shape = v ? std::get_if<Value::Shape>(&v->value) : nullptr;
                          if (!shape) throw std::runtime_error("Property named after templateShapeName is required.");
                          auto scaled = [&](const Value::LocationMm& m) { return cv::Point2d(m.x * ctx.pixelsPerMmX, m.y * ctx.pixelsPerMmY); };
                          const cv::Rect2d b = JPStageUtil::bounds(shape->outlines, scaled);
                          if (b.width == 0 || b.height == 0)
                              throw std::runtime_error("Invalid shape found, unable to create template for shape match.");
                          const double over = s.number("oversize") <= 0 ? 1.5 : s.number("oversize");
                          const double width = std::ceil(b.width * over), height = std::ceil(b.height * over);
                          Output out;
                          out.image = cv::Mat(int(height), int(width), CV_8UC3, cv::Scalar::all(0));
                          JPStageUtil::fill(out.image, shape->outlines, cv::Scalar::all(255),
                               [&](const Value::LocationMm& m) { return scaled(m) + cv::Point2d(width / 2, height / 2); });
                          out.colorSpace = "Bgr";
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "CreateFootprintTemplateImage", "",
                      "Creates a template from the specified footprint and camera properties. The template is scaled to the camera's units.",
                      { P { "footprint-view", Kind::Choice, "Fiducial",
                            "Determines, how the footprint is drawn. Fiducial: only draws the pads, TopView: draws body over pads, BottomView: draws pads over body.",
                            { "Fiducial", "TopView", "BottomView" } },
                        P { "pads-color", Kind::Color, "255,255,255,255", "Color of the pads." },
                        P { "body-color", Kind::Color, "0,0,0,255", "Color of the body." },
                        P { "background-color", Kind::Color, "0,0,0,255", "Color of the background." },
                        P { "minimal-image-size", Kind::Flag, "false", "If enabled dimensions are only controled by the part size." },
                        P { "x-offset", Kind::Number, "0.0", "Vision offset in X in pixels. The footprint is visually asymmetric, and its detection center offset to the right (+) or left (-)." },
                        P { "y-offset", Kind::Number, "0.0", "Vision offset in Y in pixels. The footprint is visually asymmetric, and its detection center offset to the top (+) or bottom (-)." },
                        P { "rotation", Kind::Number, "0.0", "Rotation" },
                        P { "max-width", Kind::Number, "0.0", "Max width of the template. Set 0 to get the full width." },
                        P { "max-height", Kind::Number, "0.0", "Max height of the template. Set 0 to get the full height." },
                        P { "property-name", Kind::Text, "footprint", "Name of the property through which OpenPnP controls this stage. Use \"footprint\" for standard control." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          std::string control = s.text("property-name");
                          if (control.empty()) control = "footprint";
                          const auto& ctx = p.context();
                          if (ctx.pixelsPerMmX <= 0 || ctx.pixelsPerMmY <= 0) throw std::runtime_error("Property \"camera\" is required.");
                          const Value* v = p.property(control);
                          const auto* footprint = v ? std::get_if<Value::Footprint>(&v->value) : nullptr;
                          if (!footprint) throw std::runtime_error("Property \"" + control + "\" is required.");
                          // Each from its own setting (OpenPnP takes the rotation's and the Y
                          // offset's default from the X offset, a slip not kept).
                          const double rotation = p.overridden(s, "rotation", s.number("rotation"), control + ".rotation");
                          const double xOffset = p.overridden(s, "x-offset", s.number("x-offset"), control + ".xOffset");
                          const double yOffset = p.overridden(s, "y-offset", s.number("y-offset"), control + ".yOffset");
                          const double maxWidth = p.overridden(s, "max-width", s.number("max-width"), control + ".maxWidth");
                          const double maxHeight = p.overridden(s, "max-height", s.number("max-height"), control + ".maxHeight");
                          const bool minimal = s.flag("minimal-image-size");
                          const double marginFactor = minimal ? 1 : 1.5, minimumMargin = minimal ? 0 : 3;
                          // Turned against the clock (Y down), moved by the offset, in pixels, Y up.
                          const double a = -rotation * M_PI / 180, ca = std::cos(a), sa = std::sin(a);
                          auto px = [&](const Value::LocationMm& m) {
                              const double x = m.x * ctx.pixelsPerMmX - xOffset, y = -m.y * ctx.pixelsPerMmY + yOffset;
                              return cv::Point2d(x * ca - y * sa, x * sa + y * ca);
                          };
                          std::vector<Value::Outline> all = footprint->pads;
                          all.push_back(footprint->body);
                          const cv::Rect2d b = JPStageUtil::bounds(all, px);
                          if (b.width == 0 || b.height == 0)
                              throw std::runtime_error("Invalid footprint found, unable to create template for part match. Width and height of pads must be greater than 0. See https://github.com/openpnp/openpnp/wiki/Fiducials.");
                          double width = std::max(b.width * marginFactor, b.width + 2 * minimumMargin);
                          double height = std::max(b.height * marginFactor, b.height + 2 * minimumMargin);
                          if (maxWidth > 0) width = std::min(maxWidth, width);
                          if (maxHeight > 0) height = std::min(maxHeight, height);
                          width = double(JPStageUtil::javaRound(width));
                          height = double(JPStageUtil::javaRound(height));
                          Output out;
                          out.image = cv::Mat(int(height), int(width), CV_8UC3, s.color("background-color"));
                          auto centred = [&](const Value::LocationMm& m) { return px(m) + cv::Point2d(width / 2, height / 2); };
                          const std::string view = s.text("footprint-view");
                          const std::vector<Value::Outline> body { footprint->body };
                          if (view == "BottomView") JPStageUtil::fill(out.image, body, s.color("body-color"), centred);
                          JPStageUtil::fill(out.image, footprint->pads, s.color("pads-color"), centred);
                          if (view == "TopView") JPStageUtil::fill(out.image, body, s.color("body-color"), centred);
                          out.colorSpace = "Bgr";
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "CreateModelTemplateImage", "Image Processing",
                      "Create a cropped template image in portrait orientation based on a model.",
                      { P { "model-stage-name", Kind::StageName, "", "Name of a prior stage to retrieve the model from." },
                        P { "degrees", Kind::Number, "0.0", "Orientation of the output image. Zero is up, angles increase clockwise." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("model-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          std::optional<cv::RotatedRect> found = rectOf(p.expectedResult(from).model);
                          if (!found) return Output {};
                          cv::RotatedRect rect = *found;
                          if (rect.size.width > rect.size.height) portrait(rect);
                          const double degrees = s.number("degrees");
                          cv::Mat map = cv::getRotationMatrix2D(rect.center, rect.angle - degrees, 1.0);
                          rect.angle = float(degrees);
                          const cv::Rect box = rect.boundingRect();
                          map.at<double>(0, 2) += box.width / 2.0 - rect.center.x;
                          map.at<double>(1, 2) += box.height / 2.0 - rect.center.y;
                          Output out;
                          cv::warpAffine(p.workingImage(), out.image, map, box.size(), cv::INTER_LINEAR);
                          rect.center = { float(box.width / 2.0), float(box.height / 2.0) };
                          out.model.value = rect;
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "ReadPartTemplateImage", "Image Processing",
                      "Read a template image from disk given a user defined file name, or infer the image's name from the id of the part loaded in the feeder and load it from a path defined by the user.",
                      { P { "template-file", Kind::Text, "", "Name of a template image, or name of a directory where an image can be found with a name inferred from part or package ID." },
                        P { "extension", Kind::Text, ".png", "Extension of image file. Defaults to '.png'." },
                        P { "prefix", Kind::Text, "", "Prefix of the filename. Used for automatic filename generation to distinguish between different uses (e.g. up/down camera). Default empty." },
                        P { "log", Kind::Flag, "false", "Enable logging." },
                        P { "color-space", Kind::Choice, "Bgr",
                            "The color space of the image.  Use to select the color space that the original image had when it was written.  Note that this does not change any of the numerical values that represent the image but rather their interpretation when the image is displayed in the pipeline editor.",
                            { "Gray", "Bgr", "Rgb", "Hls", "HlsFull", "Hsv", "HsvFull" } } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          std::string extension = s.text("extension");
                          if (JPStageUtil::blank(extension)) extension = ".png";
                          const bool log = s.flag("log");
                          std::string path = templatePath(p, s.text("template-file"), "");
                          std::error_code ec;
                          if (!endsWith(path, extension)) {
                              // A directory: the part's own template, else its package's, else its body.
                              const Value::Part* part = partOf(p);
                              if (!part) {
                                  if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "No feeder, part, or useable templateFile found. Cannot figure out part name.";
                                  return Output {};
                              }
                              if (path.back() != '/') path += '/';
                              const std::string byPart = path + s.text("prefix") + part->id + extension;
                              const std::string byPackage = path + s.text("prefix") + part->packageId + extension;
                              if (std::filesystem::exists(byPart, ec)) {
                                  if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "Using part template image.";
                                  path = byPart;
                              } else if (std::filesystem::exists(byPackage, ec)) {
                                  if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "Using package template image.";
                                  path = byPackage;
                              } else {
                                  if (!part->hasFootprint) return Output {};
                                  double width = part->bodyWidthMm, height = part->bodyHeightMm;
                                  if (width == 0 || height == 0) {
                                      if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "Package body dimensions are not set.";
                                      return Output {};
                                  }
                                  if (width > height) std::swap(width, height);
                                  const auto& ctx = p.context();
                                  if (ctx.pixelsPerMmX <= 0 || ctx.pixelsPerMmY <= 0) throw std::runtime_error("Property \"camera\" is required.");
                                  width *= ctx.pixelsPerMmX;
                                  height *= ctx.pixelsPerMmY;
                                  if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "Using package body as a template.";
                                  Output out;
                                  out.image = cv::Mat(int(height), int(width), CV_8UC3, cv::Scalar::all(255));
                                  out.colorSpace = "Bgr";
                                  out.model.value = cv::RotatedRect(cv::Point2f(float(width / 2), float(height / 2)), cv::Size2f(float(width), float(height)), 0);
                                  return out;
                              }
                          } else if (log) {
                              JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "Using user defined template image.";
                          }
                          if (!std::filesystem::exists(path, ec)) return Output {};
                          Output out;
                          out.image = JPStageUtil::readPicture(path);
                          if (out.image.empty()) return Output {};
                          out.colorSpace = out.image.channels() == 1 ? "Gray" : s.text("color-space");
                          const float w = float(out.image.cols), h = float(out.image.rows);
                          out.model.value = cv::RotatedRect(cv::Point2f(w / 2, h / 2), cv::Size2f(w, h), 0);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "WritePartTemplateImage", "Image Processing",
                      "Write a template image to disk given a user defined file name, or infer the image's name from the id of the part loaded in the feeder and write it to the path defined by the user.",
                      { P { "template-file", Kind::Text, "", "Name of the template image to write, or name of a directory where the image should be written with a name inferred from the part ID." },
                        P { "extension", Kind::Text, ".png", "Extension of image file. Defaults to '.png'." },
                        P { "prefix", Kind::Text, "", "Prefix of the filename. Used for automatic filename generation to distinguish between different uses (e.g. up/down camera). Default empty." },
                        P { "as-package", Kind::Flag, "false", "Write image as a package template." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string extension = s.text("extension");
                          std::string path = templatePath(p, s.text("template-file"), "/new");
                          std::error_code ec;
                          if (endsWith(path, extension)) {
                              const std::filesystem::path parent = std::filesystem::path(path).parent_path();
                              if (!parent.empty()) std::filesystem::create_directories(parent, ec);
                          } else {
                              const Value::Part* part = partOf(p);
                              if (!part) throw std::runtime_error("No feeder, part, or useable templateFile found. Cannot figure out part name.");
                              std::filesystem::create_directories(path, ec);
                              if (path.back() != '/') path += '/';
                              path += s.text("prefix") + (s.flag("as-package") ? part->packageId : part->id) + extension;
                          }
                          JPStageUtil::writePicture(path, p.workingImage());
                          return Output {};
                      } });
    for (const bool many : { false, true })
        types.push_back({ std::string(kStages) + (many ? "MatchPartsTemplate" : "MatchPartTemplate"), "Image Processing",
                          many ? "OpenCV based image template matching with local maxima detection improvements. On match, returns the true orientation of the input models."
                               : "OpenCV based image template matching with local maxima detection improvements. On match, returns the true orientation of the input model.",
                          { P { "log", Kind::Flag, "false", "Enable logging." },
                            P { "template-stage-name", Kind::StageName, "", "Name of a prior stage to load the template image from." },
                            P { "model-stage-name", Kind::StageName, "", "Name of a prior stage to load the working model from." },
                            P { "threshold", Kind::Number, many ? "0.85" : "0.4000000059604645",
                                many ? "If maximum value is below this value, then no matches will be reported. Default is 0.85."
                                     : "If maximum value is below this value, then no matches will be reported. Default is 0.4." } },
                          [many](JPPipeline& p, JPPipelineStage& s) {
                              const bool log = s.flag("log");
                              const std::string modelFrom = s.text("model-stage-name"), templateFrom = s.text("template-stage-name");
                              if (JPStageUtil::blank(modelFrom) || JPStageUtil::blank(templateFrom)) {
                                  if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "No input image or model was found.";
                                  return Output {};
                              }
                              const Model& model = p.expectedResult(modelFrom).model;
                              const JPPipeline::Result& templ = p.expectedResult(templateFrom);
                              if (templ.image.empty()) return Output {};
                              const cv::Mat original = p.workingImage().clone();
                              std::vector<cv::RotatedRect> found;
                              if (many) {
                                  std::vector<cv::RotatedRect> rects;
                                  if (const auto* r = std::get_if<cv::RotatedRect>(&model.value)) rects.push_back(*r);
                                  else if (const auto* l = std::get_if<std::vector<cv::RotatedRect>>(&model.value)) rects = *l;
                                  else if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "No model was found. A RotatedRect was expected.";
                                  for (const cv::RotatedRect& r : rects)
                                      if (auto t = turnedByTemplate(original, templ, r, s.number("threshold"), true, log)) found.push_back(*t);
                              } else {
                                  const std::optional<cv::RotatedRect> r = rectOf(model);
                                  if (!r) {
                                      if (log) JLOGC(JPlacerLog::kPipeline, JLogLevel::Info) << "No model was found. A RotatedRect was expected.";
                                      return Output {};
                                  }
                                  const auto t = turnedByTemplate(original, templ, *r, s.number("threshold"), false, log);
                                  if (!t) return Output {};
                                  found.push_back(*t);
                              }
                              Output out;
                              out.image = original;
                              out.model.value = found;
                              return out;
                          } });
}

} // inline namespace jf
