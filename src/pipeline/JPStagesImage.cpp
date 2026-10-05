// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's stages that bring a picture in or keep one: the camera's
// capture, a file read or written (PNG), a picture written while debugging,
// an earlier stage's picture recalled, two added, a model's property read,
// and the size check.

#include "JPPipeline.h"
#include "JPStageRegistry.h"

#include "camera/JPImageFile.h"
#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

bool blank(const std::string& s) { return s.find_first_not_of(" \t") == std::string::npos; }

// A picture file as OpenCV wants it (BGR), and back.
cv::Mat readPicture(const std::string& path) {
    JPFrame frame;
    std::string error;
    if (!JPImageFile::readPng(path, frame, error)) throw std::runtime_error(error);
    cv::Mat rgba(frame.height, frame.width, CV_8UC4, frame.rgba.data());
    cv::Mat bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    return bgr;
}

void writePicture(const std::string& path, const cv::Mat& mat) {
    cv::Mat rgba;
    if (mat.channels() == 1) cv::cvtColor(mat, rgba, cv::COLOR_GRAY2RGBA);
    else if (mat.channels() == 3) cv::cvtColor(mat, rgba, cv::COLOR_BGR2RGBA);
    else cv::cvtColor(mat, rgba, cv::COLOR_BGRA2RGBA);
    if (rgba.depth() != CV_8U) rgba.convertTo(rgba, CV_8U);
    JPFrame frame;
    frame.width = rgba.cols;
    frame.height = rgba.rows;
    frame.rgba.assign(rgba.data, rgba.data + rgba.total() * 4);
    std::string error;
    if (!JPImageFile::writePng(path, frame, error)) throw std::runtime_error(error);
}

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
                          out.image = readPicture(file);
                          const auto& ctx = p.context();
                          // As if the camera took it: at its size.
                          if (s.flag("handle-as-captured") && ctx.cameraWidth > 0 && ctx.cameraHeight > 0
                              && (out.image.cols != ctx.cameraWidth || out.image.rows != ctx.cameraHeight))
                              cv::resize(out.image, out.image, cv::Size(ctx.cameraWidth, ctx.cameraHeight));
                          out.colorSpace = out.image.channels() == 1 ? "Gray" : s.text("color-space");
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "ImageWrite", "", "", { P { "file", Kind::Text, "", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          writePicture(s.text("file"), p.workingImage());
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "ImageWriteDebug", "", "",
                      { P { "prefix", Kind::Text, "debug", "" }, P { "suffix", Kind::Text, ".png", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          // Only while the pipeline's log says debug.
                          if (!JLog::instance().enabled(JPlacerLog::kPipeline, JLogLevel::Debug) || p.context().debugDirectory.empty())
                              return Output {};
                          const std::string dir = p.context().debugDirectory;
                          std::error_code ec;
                          std::filesystem::create_directories(dir, ec);
                          const long long nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                      std::chrono::system_clock::now().time_since_epoch()).count();
                          writePicture(dir + "/" + s.text("prefix") + std::to_string(nanos) + s.text("suffix"), p.workingImage());
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "ImageRecall", "", "", { P { "image-stage-name", Kind::StageName, "", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const std::string from = s.text("image-stage-name");
                          if (blank(from)) return Output {};
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
                          if (blank(a) || blank(b)) return Output {};
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
                          if (blank(from)) throw std::runtime_error("modelStageName is required.");
                          if (blank(name)) throw std::runtime_error("propertyName is required.");
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
