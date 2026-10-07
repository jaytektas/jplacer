// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's stages that bring a picture in or keep one: the camera's
// capture, a file read or written (PNG), a picture written while debugging,
// an earlier stage's picture recalled, two added, a model's property read,
// and the size check.

#include "JPPipeline.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"
#include "JPVisionDebug.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <sstream>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

} // namespace

void JPStageRegistry::addImageStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "ImageCapture", "Image Processing", "Capture an image from the pipeline camera.",
                      { P { "default-light", Kind::Flag, "true", "Use the default camera lighting." },
                        P { "light", Kind::Text, "", "Light actuator value or profile, if default camera lighting is disabled." },
                        P { "settle-option", Kind::Choice, "Settle", "Wait for the camera to settle before capturing an image.",
                            { "Skip", "Settle", "SettleFullArea" } },
                        P { "count", Kind::Integer, "1", "Number of camera images to average." } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const auto& ctx = p.context();
                          if (!ctx.capture) throw JPPipeline::Terminal("No Camera set on pipeline.");
                          const std::string light = s.flag("default-light") ? std::string() : s.text("light");
                          cv::Mat image;
                          std::string why;
                          if (!ctx.capture(s.text("settle-option"), light, image, why)) throw JPPipeline::Terminal(why);
                          const int count = std::max(1, s.integer("count"));
                          Output out;
                          out.colorSpace = "Bgr";
                          if (count <= 1) {
                              out.image = image;
                              return out;
                          }
                          // The average of `count` pictures, the later ones as they come.
                          cv::Mat sum;
                          image.convertTo(sum, CV_64F, 1.0 / count);
                          for (int i = 1; i < count; ++i) {
                              cv::Mat next;
                              if (!ctx.capture("Skip", light, next, why)) throw JPPipeline::Terminal(why);
                              cv::Mat scaled;
                              next.convertTo(scaled, CV_64F, 1.0 / count);
                              sum += scaled;
                          }
                          sum.convertTo(out.image, CV_8U);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "ImageRead", "Image Processing",
                      "Replace the working image with the image loaded from a given path.",
                      { P { "file", Kind::Text, "", "Absolute path of the image file to read." },
                        P { "color-space", Kind::Choice, "Bgr",
                            "The color space of the image.  Use to select the color space that the original image had when it was written. Note that this does not change any of the numerical values that represent the image but rather their interpretation when the image is displayed in the pipeline editor.",
                            { "Gray", "Bgr", "Rgb", "Hls", "HlsFull", "Hsv", "HsvFull" } },
                        P { "handle-as-captured", Kind::Flag, "false",
                            "Handle the loaded image as if captured by the camera. The image resolution and aspect ratio will be adapted, so any pixel coordinates obtained from the image are correctly interpreted, as they would from a camera captured image." } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const std::string file = s.text("file");
                          std::error_code ec;
                          if (!std::filesystem::exists(file, ec)) return Output {};
                          Output out;
                          out.image = JPStageUtil::readPicture(file);
                          const auto& ctx = p.context();
                          // As if the camera took it: at its size.
                          if (s.flag("handle-as-captured") && ctx.cameraWidth > 0 && ctx.cameraHeight > 0
                              && (out.image.cols != ctx.cameraWidth || out.image.rows != ctx.cameraHeight))
                              cv::resize(out.image, out.image, cv::Size(ctx.cameraWidth, ctx.cameraHeight));
                          out.colorSpace = out.image.channels() == 1 ? "Gray" : s.text("color-space");
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "ActuatorWrite", "", "Performs simple actuator write. Machine must be connected, otherwise error is thrown.",
                      { P { "actuator-name", Kind::Text, "", "" } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string name = s.text("actuator-name");
                          if (name.empty()) {
                              JLOGC(JPlacerLog::kPipeline, JLogLevel::Warn) << "No actuator name specified for pipeline " << s.name() << ".";
                              return Output {};
                          }
                          const auto& ctx = p.context();
                          if (!ctx.actuatorExists || !ctx.actuatorExists(name))
                              throw std::runtime_error("Actuator writing (CvStage operation) failed. Unable to find an actuator named " + name);
                          // The value: its element, or as older files wrote it (a type and a number).
                          std::string value = "true";
                          if (const JPXmlNode* e = s.element("actuator-write-value")) value = e->text;
                          else if (const std::string* type = s.toXml().get("actuator-type")) {
                              const double v = s.number("actuator-value");
                              std::ostringstream t;
                              t << v;
                              value = *type == "Boolean" ? (v != 0.0 ? "true" : "false") : t.str();
                          }
                          std::string why;
                          // A machine that fails stops the pipeline.
                          if (!ctx.actuate || !ctx.actuate(name, value, why)) throw JPPipeline::Terminal(why);
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "ScriptRun", "",
                      "Run an arbitrary script file using the built in scripting engine. pipeline and stage are exposed as globals for use by the script. To return a pipeline result you can't use a return statement, but instead just let the object be the last thing the script evaluates.",
                      { P { "file", Kind::Text, "", "" }, P { "args", Kind::Text, "", "" } },
                      [](JPPipeline&, JPPipelineStage& s) {
                          const std::string file = s.text("file");
                          std::error_code ec;
                          if (file.empty() || !std::filesystem::exists(file, ec)) return Output {};
                          // As OpenPnP when no engine takes the file's extension: jplacer has none.
                          throw std::runtime_error("Unable to find scriping engine for " + file);
                      } });
    types.push_back({ std::string(kStages) + "ImageWrite", "", "", { P { "file", Kind::Text, "", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          JPStageUtil::writePicture(s.text("file"), p.workingImage());
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "ImageWriteDebug", "", "",
                      { P { "prefix", Kind::Text, "debug", "" }, P { "suffix", Kind::Text, ".png", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          // As OpenPnP's, only while debugging: vision debugging on (Preferences), into its folder
                          // (else the pipeline's own, given with the pipeline's log at debug).
                          const bool logged = JLog::instance().enabled(JPlacerLog::kPipeline, JLogLevel::Debug);
                          const std::string dir = JPVisionDebug::on() ? JPVisionDebug::imageWriteDebugDirectory()
                                                : logged              ? p.context().debugDirectory
                                                                      : std::string();
                          if (dir.empty()) return Output {};
                          std::error_code ec;
                          std::filesystem::create_directories(dir, ec);
                          const long long nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                      std::chrono::system_clock::now().time_since_epoch()).count();
                          JPStageUtil::writePicture(dir + "/" + s.text("prefix") + std::to_string(nanos) + s.text("suffix"), p.workingImage());
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "ImageRecall", "", "", { P { "image-stage-name", Kind::StageName, "", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const std::string from = s.text("image-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const JPPipeline::Result& r = p.expectedResult(from);
                          if (r.image.empty()) return Output {};
                          Output out;
                          out.image = r.image.clone();
                          out.colorSpace = r.colorSpace;
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "Add", "", "Adds two images together, scale either, or subtract second.",
                      { P { "first-stage-name", Kind::StageName, "", "" }, P { "second-stage-name", Kind::StageName, "", "" },
                        P { "first-scalar", Kind::Number, "1.0", "" }, P { "second-scalar", Kind::Number, "1.0", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const std::string a = s.text("first-stage-name"), b = s.text("second-stage-name");
                          if (JPStageUtil::blank(a) || JPStageUtil::blank(b)) return Output {};
                          const cv::Mat& first = p.expectedResult(a).image;
                          const cv::Mat& second = p.expectedResult(b).image;
                          const double fs = s.number("first-scalar"), ss = s.number("second-scalar");
                          if (fs < 0) throw std::runtime_error("firstScalar < 0!");
                          cv::Mat f = first.clone(), sc = second.clone();
                          if (fs != 1.0) cv::multiply(first, cv::Scalar::all(fs), f);
                          if (ss != 1.0) cv::multiply(second, cv::Scalar::all(std::abs(ss)), sc);
                          Output out;
                          if (ss > 0) cv::add(f, sc, out.image);
                          else cv::subtract(f, sc, out.image);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "ReadModelProperty", "", "",
                      { P { "model-stage-name", Kind::StageName, "", "" }, P { "property-name", Kind::Text, "", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const std::string from = s.text("model-stage-name"), name = s.text("property-name");
                          if (JPStageUtil::blank(from)) throw std::runtime_error("modelStageName is required.");
                          if (JPStageUtil::blank(name)) throw std::runtime_error("propertyName is required.");
                          const JPPipelineModel& m = p.expectedResult(from).model;
                          Output out;
                          // The bean properties OpenPnP's models have.
                          if (const auto* r = std::get_if<cv::RotatedRect>(&m.value)) {
                              if (name == "center") out.model.value = cv::Point2d(r->center);
                              else if (name == "angle") out.model.value = double(r->angle);
                              else throw std::runtime_error("RotatedRect has no property " + name);
                          } else if (const auto* pt = std::get_if<cv::Point2d>(&m.value)) {
                              if (name == "x") out.model.value = pt->x;
                              else if (name == "y") out.model.value = pt->y;
                              else throw std::runtime_error("Point has no property " + name);
                          } else {
                              throw std::runtime_error(m.kind() + " has no property " + name);
                          }
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "SizeCheck", "",
                      "It uses pixel sizes. Check part's pixel sizes on detected RotatedRect (i.e. MinAreaRect) and put the same values into this stage. Place this stage after the Pipeline that returns RotatedRect (i.e. MinAreaRect...) and use it as 'results' stage. Rename MinAreaRect into 'result'.",
                      { P { "tolerance", Kind::Integer, "5", "This value is inaccuracy tolerance +/- pixel size that is considered to be valid." },
                        P { "size-w", Kind::Integer, "7", "1st component's size (pixels)." },
                        P { "size-h", Kind::Integer, "28", "2nd component's size (pixels)." } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const auto* r = std::get_if<cv::RotatedRect>(&p.workingModel().value);
                          if (!r) throw std::runtime_error("The working model is not a RotatedRect.");
                          const int tol = s.integer("tolerance"), w = s.integer("size-w"), h = s.integer("size-h");
                          Output out;
                          if (std::abs(std::max(r->size.width, r->size.height) - std::max(w, h)) <= tol
                              && std::abs(std::min(r->size.width, r->size.height) - std::min(w, h)) <= tol)
                              out.model.value = *r;
                          return out;
                      } });
}

} // inline namespace jf
