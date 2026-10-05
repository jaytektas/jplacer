// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's symmetry stages: the most circularly symmetric places (nozzle
// tips, fiducials), and the subject of the most rectlinear symmetry (a part
// on the nozzle), with what the vision operation sets in their place.

#include "JPCircularSymmetry.h"
#include "JPPipeline.h"
#include "JPRectlinearSymmetry.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"

#include <cmath>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

const std::vector<std::string> kFunctions { "FullSymmetry", "EdgeSymmetry", "OutlineSymmetry", "OutlineEdgeSymmetry", "OutlineSymmetryMasked" };
const char* const kFunctionHelp =
    "<li><strong>FullSymmetry</strong> looks for full inner and outline symmetry. Use for truly symmetric subjects and best precision.</li>"
    "<li><strong>EdgeSymmetry</strong> looks for full inner and outline symmetry of edges. Use for partially symmetric subjects, where some, but not all features are present on both sides, or where shades differ.</li>"
    "<li><strong>OutlineSymmetry</strong> looks for outline symmetry only. Used for subjects that are symmetric on their outline, but not on the inside.</li>"
    "<li><strong>OutlineEdgeSymmetry</strong> looks for outline symmetry of edges only. Used for subjects that are symmetric on their outline, but not on the inside, and where some, but not all features are present on both sides, or where shades differ.</li>"
    "<li><strong>OutlineSymmetryMasked</strong> looks for outline mask symmetry only. Use for quite asymmetric subjects. Requires setting a mask <strong>threshold</strong>.<br/></li></ul>";

JPRectlinearSymmetry::Function functionOf(const std::string& name) {
    for (size_t i = 0; i < kFunctions.size(); ++i)
        if (name == kFunctions[i]) return JPRectlinearSymmetry::Function(i);
    return JPRectlinearSymmetry::Function::FullSymmetry;
}

} // namespace

void JPStageRegistry::addSymmetryStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "DetectCircularSymmetry", "",
                      "Finds circular symmetry in the working image. Diameter range and maximum search distance can be specified.",
                      { P { "min-diameter", Kind::Integer, "10", "Minimum diameter of the circle, in pixels." },
                        P { "max-diameter", Kind::Integer, "100", "Maximum diameter of the circle, in pixels." },
                        P { "max-distance", Kind::Integer, "100", "Maximum search distance (radius) from nominal center, in pixels." },
                        P { "search-width", Kind::Integer, "0", "Maximum search width across nominal center, in pixels (0 = same as maximum search distance × 2)." },
                        P { "search-height", Kind::Integer, "0", "Maximum search height across nominal center, in pixels (0 = same as maximum search distance × 2)." },
                        P { "max-target-count", Kind::Integer, "1", "Maximum number of targets to be found." },
                        P { "min-symmetry", Kind::Number, "1.2", "Minimum relative circular symmetry (overall pixel variance vs. circular pixel variance)." },
                        P { "corr-symmetry", Kind::Number, "0.0", "Correlated minimum circular symmetry for multiple matches, i.e. other matches must have at least this relative symmetry. Must be in the interval [0,1]. " },
                        P { "outer-margin", Kind::Number, "0.2", "Relative outer diameter margin used when the propertyName is set." },
                        P { "inner-margin", Kind::Number, "0.4", "Relative inner diameter margin used when the propertyName is set." },
                        P { "sub-sampling", Kind::Integer, "8",
                            "To speed things up, only one pixel out of a square of subSampling × subSampling pixels is sampled. The best region is then locally searched using iteration with smaller and smaller subSampling size.<br/>The subSampling value will automatically be reduced for small diameters. Use BlurGaussian before this stage if subSampling suffers from moiré effects." },
                        P { "super-sampling", Kind::Integer, "1", "The superSampling value can be used to achieve sub-pixel final precision: 1 means no supersampling, 2 means half sub-pixel precision etc." },
                        P { "symmetry-score", Kind::Choice, "OverallVarianceVsRingVarianceSum",
                            "Set the symmetry score calculation method.<br/><ul><li>Overall variance vs. ring variance sum (default): tolerant circular symmetry score. Matches partial/scattered circular patterns.</li><li>Ring ageraged variance vs. ring variance sum: stricter circular symmetry score. The rings must be quite uniform to match.</li><li>Ring median variance vs. ring variance sum: even stricter circular symmetry score. Rejects interrupted rings, e.g. from tangential non-circular shapes.</li></ul>",
                            { "OverallVarianceVsRingVarianceSum", "RingAvgeragesVarianceVsRingVarianceSum", "RingMedianVarianceVsRingVarianceSum" } },
                        P { "property-name", Kind::Text, "",
                            "Property name as controlled by the vision operation using this pipeline.<br/><ul><li><i>propertyName</i>.diameter</li><li><i>propertyName</i>.maxDistance</li><li><i>propertyName</i>.center</li></ul>If set, these will override the properties configured here." },
                        P { "diagnostics", Kind::Flag, "false", "Display matches with circle and cross-hairs." },
                        P { "heat-map", Kind::Flag, "false", "Overlay a heat map indicating the local circular symmetry." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          cv::Mat& mat = p.workingImage();
                          JPCircularSymmetry::Search q;
                          q.minDiameter = s.integer("min-diameter");
                          q.maxDiameter = s.integer("max-diameter");
                          int maxDistance = s.integer("max-distance");
                          q.searchWidth = s.integer("search-width");
                          q.searchHeight = s.integer("search-height");
                          cv::Point2d center(mat.cols * 0.5, mat.rows * 0.5);
                          const std::string control = s.text("property-name");
                          if (!control.empty()) {
                              const double diameter = p.overridden(s, "diameter", NAN, control + ".diameter");
                              if (std::isfinite(diameter)) {
                                  q.minDiameter = int(JPStageUtil::javaRound(diameter * (1.0 - s.number("inner-margin"))));
                                  q.maxDiameter = int(JPStageUtil::javaRound(diameter * (1.0 + s.number("outer-margin"))));
                                  p.noteOverride(s.name(), "min-diameter", std::to_string(q.minDiameter));
                                  p.noteOverride(s.name(), "max-diameter", std::to_string(q.maxDiameter));
                              }
                              maxDistance = int(p.overridden(s, "max-distance", maxDistance, control + ".maxDistance"));
                              q.searchWidth = int(p.overridden(s, "search-width", q.searchWidth, control + ".searchWidth"));
                              q.searchHeight = int(p.overridden(s, "search-height", q.searchHeight, control + ".searchHeight"));
                              center = p.overriddenPoint(s, "center", center, control + ".center");
                          }
                          if (q.searchWidth <= 0) q.searchWidth = maxDistance * 2;
                          if (q.searchHeight <= 0) q.searchHeight = maxDistance * 2;
                          q.xCenter = int(center.x);
                          q.yCenter = int(center.y);
                          q.searchDiameter = maxDistance * 2;
                          q.maxTargetCount = s.integer("max-target-count");
                          q.minSymmetry = s.number("min-symmetry");
                          q.corrSymmetry = s.number("corr-symmetry");
                          q.subSampling = s.integer("sub-sampling");
                          q.superSampling = s.integer("super-sampling");
                          const std::string score = s.text("symmetry-score");
                          q.score = score == "RingAvgeragesVarianceVsRingVarianceSum"  ? JPCircularSymmetry::Score::RingAvgeragesVarianceVsRingVarianceSum
                                    : score == "RingMedianVarianceVsRingVarianceSum" ? JPCircularSymmetry::Score::RingMedianVarianceVsRingVarianceSum
                                                                                      : JPCircularSymmetry::Score::OverallVarianceVsRingVarianceSum;
                          q.diagnostics = s.flag("diagnostics");
                          q.heatMap = s.flag("heat-map");
                          JPCircularSymmetry::ScoreRange range;
                          Output out;
                          out.model.value = JPCircularSymmetry::find(mat, q, range);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "DetectRectlinearSymmetry", "",
                      "Finds the subject and angle with maximum rectlinear symmetry in the working image. This is achieved by first computing reclinear, i.e. horizontal and vertical cross-sections at various angles. The angle with the largest contrasts is then used.<br/>As a second step, the mid-points with the largest symmetry in the horizontal and vertical cross-sections is determined to detect the subject center.",
                      { P { "expected-angle", Kind::Number, "0.0", "Expected angle of the subject to be detected." },
                        P { "search-distance", Kind::Number, "100.0", "Search distance around the center." },
                        P { "search-angle", Kind::Number, "45.0", "Search angle, two-sided around the expected angle." },
                        P { "max-width", Kind::Number, "100.0", "Maximum cross-section width of the subject to be detected." },
                        P { "max-height", Kind::Number, "100.0", "Maximum cross-section height of the subject to be detected." },
                        P { "symmetric-left-right", Kind::Flag, "true",
                            "Tells the stage whether the subject is left/right-symmetric (in the sense as seen at 0° subject rotation). According to this switch, the stage either takes the <strong>symmetricFunction</strong>, or the <strong>asymmetricFunction</strong>." },
                        P { "symmetric-upper-lower", Kind::Flag, "true",
                            "Tells the stage whether the subject is upper/lower-symmetric (in the sense as as seen at 0° subject rotation). According to this switch, the stage either takes the <strong>symmetricFunction</strong>, or the <strong>asymmetricFunction</strong>." },
                        P { "symmetric-function", Kind::Choice, "FullSymmetry",
                            std::string("Determines how the cross-section is evaluated for <strong>symmetric</strong> subjects.<br/><ul>") + kFunctionHelp, kFunctions },
                        P { "asymmetric-function", Kind::Choice, "OutlineSymmetryMasked",
                            std::string("Determines how the cross-section is evaluated for <strong>asymmetric</strong> subjects.<br/><ul>") + kFunctionHelp, kFunctions },
                        P { "min-symmetry", Kind::Number, "10.0", "Minimum relative symmetry. Values larger than 1.0 indicate symmetry." },
                        P { "sub-sampling", Kind::Integer, "8",
                            "To speed things up, only one pixel out of a square of subSampling × subSampling pixels is sampled. The best region is then locally searched using iteration with smaller and smaller subSampling size.<br/>The subSampling value will automatically be reduced when other search properties require it. Use BlurGaussian before this stage if subSampling suffers from moiré effects." },
                        P { "super-sampling", Kind::Integer, "1",
                            "The superSampling value can be used to achieve sub-pixel final precision:<br/>1 means no supersampling, 2 means half sub-pixel precision etc.<br/>Negative values can be used to stop refining subSampling, -2 means it will stop at a 2-pixel resolution." },
                        P { "smoothing", Kind::Integer, "5",
                            "Smoothing applied to the sampled cross-sections. Given as the Gaussian kernel size.<br/>This is needed to eliminate interferences, when angular sampling coincides with the pixel raster or its diagonals." },
                        P { "gamma", Kind::Number, "2.5",
                            "Gamma to be applied to the image. The input signal is raised to the power gamma. With gammas > 1.0 the bright image parts are emphasized." },
                        P { "threshold", Kind::Integer, "128",
                            "When the <strong>OutlineSymmetryMasked</strong> function is used, only pixels with luminance greater than the <strong>threshold</strong> are considered when determining the outline of the part." },
                        P { "min-feature-size", Kind::Number, "40.0",
                            "When the <strong>OutlineSymmetryMasked</strong> function is used, pixels are masked by the <strong>threshold</strong> property. A cross-section pixel count over these masked pixels is then determined. A cross-section bin only counts as \"detected\" when the pixel count is larger than <strong>minFeatureSize</strong>.<br/>This is used to remove masking imperfections, i.e. image specks and impurities up to a certain size and frequency." },
                        P { "diagnostics", Kind::Flag, "false", "Display the detection with cross-hairs and bounds." },
                        P { "diagnostics-map", Kind::Flag, "false", "Overlay a diagnostic map indicating the angular reclinear contrast and rectlinear cross-section." },
                        P { "property-name", Kind::Text, "DetectRectlinearSymmetry",
                            "determines the pipeline property name under which this stage is controlled by the vision operation. If set, these will override some of the properties configured here. Use \"DetectRectlinearSymmetry\" for default control." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          cv::Mat& mat = p.workingImage();
                          cv::Point2d center(mat.cols * 0.5, mat.rows * 0.5);
                          JPRectlinearSymmetry::Search q;
                          q.expectedAngle = s.number("expected-angle");
                          q.searchAngle = s.number("search-angle");
                          q.searchDistance = s.number("search-distance");
                          q.maxWidth = s.number("max-width");
                          q.maxHeight = s.number("max-height");
                          q.minFeatureSize = s.number("min-feature-size");
                          q.threshold = s.integer("threshold");
                          bool leftRight = s.flag("symmetric-left-right"), upperLower = s.flag("symmetric-upper-lower");
                          q.subSampling = s.integer("sub-sampling");
                          q.superSampling = s.integer("super-sampling");
                          std::string control = s.text("property-name");
                          if (!control.empty()) {
                              // Older files' name for it.
                              if (control == "alignment") control = "DetectRectlinearSymmetry";
                              center = p.overriddenPoint(s, "center", center, control + ".center");
                              q.expectedAngle = p.overridden(s, "expected-angle", q.expectedAngle, control + ".expectedAngle");
                              q.searchAngle = p.overridden(s, "search-angle", q.searchAngle, control + ".searchAngle");
                              q.searchDistance = p.overridden(s, "search-distance", q.searchDistance, control + ".searchDistance");
                              q.maxWidth = p.overridden(s, "max-width", q.maxWidth, control + ".maxWidth");
                              q.maxHeight = p.overridden(s, "max-height", q.maxHeight, control + ".maxHeight");
                              q.minFeatureSize = p.overridden(s, "min-feature-size", q.minFeatureSize, control + ".minFeatureSize");
                              q.threshold = int(p.overridden(s, "threshold", q.threshold, control + ".threshold"));
                              leftRight = p.overriddenFlag(s, "symmetric-left-right", leftRight, control + ".symmetricLeftRight");
                              upperLower = p.overriddenFlag(s, "symmetric-upper-lower", upperLower, control + ".symmetricUpperLower");
                              q.subSampling = int(p.overridden(s, "sub-sampling", q.subSampling, control + ".subSampling"));
                              q.superSampling = int(p.overridden(s, "super-sampling", q.superSampling, control + ".superSampling"));
                          }
                          q.xCenter = int(center.x);
                          q.yCenter = int(center.y);
                          q.minSymmetry = s.number("min-symmetry");
                          q.xFunction = functionOf(s.text(leftRight ? "symmetric-function" : "asymmetric-function"));
                          q.yFunction = functionOf(s.text(upperLower ? "symmetric-function" : "asymmetric-function"));
                          q.smoothing = s.integer("smoothing");
                          q.gamma = s.number("gamma");
                          q.diagnostics = s.flag("diagnostics");
                          q.diagnosticsMap = s.flag("diagnostics-map");
                          JPRectlinearSymmetry::ScoreRange range;
                          Output out;
                          if (const auto rect = JPRectlinearSymmetry::find(mat, q, range)) out.model.value = *rect;
                          return out;
                      } });
}

} // inline namespace jf
