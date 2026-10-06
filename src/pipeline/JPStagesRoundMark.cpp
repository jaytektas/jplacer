// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// jplacer's own stage: a round mark (a fiducial, a calibration mark) found
// and measured as jplacer's round mark finder does (JPRoundMarkFinder): the
// most circular things of the size near where it should be, each measured on
// its edge all the way round to a fraction of a pixel, the best kept. With
// what the vision operation sets in its place, as DetectCircularSymmetry.

#include "JPPipeline.h"
#include "JPStageRegistry.h"

#include "vision/JPRoundMarkFinder.h"

#include <opencv2/imgproc.hpp>

#include <cmath>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;

JPGrayImage grayOf(const cv::Mat& mat) {
    cv::Mat gray;
    if (mat.channels() == 3) cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    else if (mat.channels() == 4) cv::cvtColor(mat, gray, cv::COLOR_BGRA2GRAY);
    else gray = mat;
    cv::Mat floats;
    gray.convertTo(floats, CV_32F);
    JPGrayImage g;
    g.width = floats.cols;
    g.height = floats.rows;
    g.pixels.resize(size_t(g.width) * size_t(g.height));
    for (int y = 0; y < g.height; ++y) {
        const float* row = floats.ptr<float>(y);
        std::copy(row, row + g.width, g.pixels.begin() + std::ptrdiff_t(y) * g.width);
    }
    return g;
}

} // namespace

void JPStageRegistry::addRoundMarkStages(std::vector<JPStageType>& types) {
    types.push_back({ "org.jplacer.vision.pipeline.stages.DetectRoundMark", "",
                      "jplacer's own: finds the round mark (a fiducial, a calibration mark) nearest a nominal center and measures "
                      "its center to a fraction of a pixel on its edge, all the way round. Of the given diameter (within the size "
                      "tolerance) when the vision operation sets one, else any diameter in the range.",
                      { P { "min-diameter", Kind::Integer, "10", "Minimum diameter of the mark, in pixels (when no diameter is set)." },
                        P { "max-diameter", Kind::Integer, "100", "Maximum diameter of the mark, in pixels (when no diameter is set)." },
                        P { "max-distance", Kind::Integer, "100", "Maximum search distance (radius) from nominal center, in pixels." },
                        P { "size-tolerance", Kind::Number, "0.25", "Accepted measured / set diameter - 1, either way." },
                        P { "min-shape", Kind::Number, "0.8", "Accepted share of the mark's edge found round, 0 to 1." },
                        P { "polarity", Kind::Choice, "Either", "Whether the mark is brighter than around it, darker, or either.",
                            { "Either", "Bright", "Dark" } },
                        P { "property-name", Kind::Text, "",
                            "Property name as controlled by the vision operation using this pipeline.<br/><ul><li><i>propertyName</i>.diameter</li><li><i>propertyName</i>.maxDistance</li><li><i>propertyName</i>.center</li><li><i>propertyName</i>.minShape</li></ul>If set, these will override the properties configured here." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          const cv::Mat& mat = p.workingImage();
                          cv::Point2d center(mat.cols * 0.5, mat.rows * 0.5);
                          double maxDistance = s.integer("max-distance"), diameter = NAN, minShape = s.number("min-shape");
                          const std::string control = s.text("property-name");
                          if (!control.empty()) {
                              diameter = p.overridden(s, "diameter", NAN, control + ".diameter");
                              maxDistance = p.overridden(s, "max-distance", maxDistance, control + ".maxDistance");
                              center = p.overriddenPoint(s, "center", center, control + ".center");
                              minShape = p.overridden(s, "min-shape", minShape, control + ".minShape");
                          }
                          const std::string polarity = s.text("polarity");
                          const JPRoundMarkFinder::Polarity pol = polarity == "Bright" ? JPRoundMarkFinder::Polarity::Bright
                                                                  : polarity == "Dark" ? JPRoundMarkFinder::Polarity::Dark
                                                                                       : JPRoundMarkFinder::Polarity::Either;
                          const JPGrayImage image = grayOf(mat);
                          JPRoundMark m;
                          if (std::isfinite(diameter) && diameter > 0) {
                              JPRoundMarkFinder::Request rq;
                              rq.expectedX = center.x;
                              rq.expectedY = center.y;
                              rq.searchRadius = maxDistance;
                              rq.diameter = diameter;
                              rq.sizeTolerance = s.number("size-tolerance");
                              rq.minShape = minShape;
                              rq.polarity = pol;
                              m = JPRoundMarkFinder::find(image, rq);
                          } else {
                              m = JPRoundMarkFinder::findAnySize(image, center.x, center.y, maxDistance, s.integer("min-diameter"),
                                                                 s.integer("max-diameter"), pol);
                          }
                          Output out;
                          if (m.found) out.model.value = std::vector<JPPipelineModel::Circle> { { m.x, m.y, m.diameter, m.shape } };
                          else out.model.value = JPPipelineModel::Failure { m.why };
                          return out;
                      } });
}

} // inline namespace jf
