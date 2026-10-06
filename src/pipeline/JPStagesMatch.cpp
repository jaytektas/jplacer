// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's template matching stages: a template (an earlier stage's
// picture) found in the working image, each local best within reach of the
// centre a match.

#include "JPPipeline.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
using Model = JPPipelineModel;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

} // namespace

void JPStageRegistry::addMatchStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "MatchTemplate", "Image Processing",
                      "OpenCV based image template matching with local maxima detection improvements.",
                      { P { "template-stage-name", Kind::StageName, "", "Name of a prior stage to load the template image from." },
                        P { "threshold", Kind::Number, "0.7", "If maximum value is below this value, then no matches will be reported. Default is 0.7." },
                        P { "corr", Kind::Number, "0.85", "Normalized minimum recognition threshold for the CCOEFF_NORMED method, in the interval [0,1]. Default is 0.85." },
                        P { "normalize", Kind::Flag, "true", "Normalize results to maximum value." },
                        P { "max-distance", Kind::Integer, "10000", "Maximum search distance (radius) from nominal center, in pixels." },
                        P { "property-name", Kind::Text, "", "Property name as controlled by the vision operation using this pipeline: propertyName.maxDistance, propertyName.center. If set, these will override the properties configured here." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const std::string from = s.text("template-stage-name");
                          if (JPStageUtil::blank(from)) return Output {};
                          const cv::Mat& mat = p.workingImage();
                          const cv::Mat& templ = p.expectedResult(from).image;
                          double maxDistance = s.integer("max-distance");
                          cv::Point2d center(mat.cols * 0.5, mat.rows * 0.5);
                          const std::string control = s.text("property-name");
                          if (!control.empty()) {
                              maxDistance = double(p.overriddenInteger(s, "max-distance", long(maxDistance), control + ".maxDistance"));
                              center = p.overriddenPoint(s, "center", center, control + ".center");
                          }
                          cv::Mat result;
                          cv::matchTemplate(mat, templ, result, cv::TM_CCOEFF_NORMED);
                          double maxVal = 0;
                          cv::minMaxLoc(result, nullptr, &maxVal);
                          const double rangeMin = std::max(s.number("threshold"), s.number("corr") * maxVal);
                          std::vector<Model::TemplateMatch> matches;
                          for (const cv::Point& pt : JPStageUtil::matMaxima(result, rangeMin, maxVal)) {
                              if (std::hypot(pt.x - center.x, pt.y - center.y) >= maxDistance) continue;
                              matches.push_back({ double(pt.x), double(pt.y), double(templ.cols), double(templ.rows),
                                                  result.at<float>(pt.y, pt.x) / (s.flag("normalize") ? maxVal : 1.0) });
                          }
                          std::stable_sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
                          Output out;
                          out.image = result;
                          out.model.value = matches;
                          return out;
                      } });
}

} // inline namespace jf
