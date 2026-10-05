// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's image processing stages that change the working image in place:
// blurs, colour conversion, thresholds, masks, normalising and equalising,
// edge detection, rotation and filling.

#include "JPPipeline.h"
#include "JPStageRegistry.h"

#include <opencv2/imgproc.hpp>

#include <cfloat>
#include <cmath>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";

// A kernel size as OpenPnP keeps it: odd, at least 3.
int oddKernel(int k) { return std::max(3, 2 * (k / 2) + 1); }

struct ColorCode {
    const char* name;
    int         code;
    const char* resulting;
};
// OpenPnP's FluentCv.ColorCode, and the colour space each leaves.
constexpr ColorCode kColorCodes[] = {
    { "Bgr2Gray", cv::COLOR_BGR2GRAY, "Gray" },       { "Rgb2Gray", cv::COLOR_RGB2GRAY, "Gray" },
    { "Gray2Bgr", cv::COLOR_GRAY2BGR, "Bgr" },        { "Gray2Rgb", cv::COLOR_GRAY2RGB, "Rgb" },
    { "Bgr2Hls", cv::COLOR_BGR2HLS, "Hls" },          { "Hls2Bgr", cv::COLOR_HLS2BGR, "Bgr" },
    { "Bgr2HlsFull", cv::COLOR_BGR2HLS_FULL, "HlsFull" }, { "Hls2BgrFull", cv::COLOR_HLS2BGR_FULL, "Bgr" },
    { "Bgr2Hsv", cv::COLOR_BGR2HSV, "Hsv" },          { "Hsv2Bgr", cv::COLOR_HSV2BGR, "Bgr" },
    { "Bgr2HsvFull", cv::COLOR_BGR2HSV_FULL, "HsvFull" }, { "Hsv2BgrFull", cv::COLOR_HSV2BGR_FULL, "Bgr" },
};

// Everything outside the mask black (OpenPnP's masks): a new image.
cv::Mat masked(const cv::Mat& mat, const cv::Mat& mask) {
    cv::Mat out(mat.size(), mat.type(), cv::Scalar::all(0));
    mat.copyTo(out, mask);
    return out;
}

// MaskHsv's auto: each channel's limits round its histogram's peak, wide
// enough to take in about `fraction` of the pixels not black (hue wrapping
// round, saturation and value not). -1, -1 when nothing is to be masked.
std::pair<int, int> autoLimits(const cv::Mat& working, int channel, double alreadyMasked, double amountToMask, bool circular) {
    constexpr int kBins = 256;
    cv::Mat ch, hist;
    cv::extractChannel(working, ch, channel);
    const int channels[] = { 0 };
    const int sizes[] = { kBins };
    const float range[] = { 0, 256 };
    const float* ranges[] = { range };
    cv::calcHist(&ch, 1, channels, cv::Mat(), hist, 1, sizes, ranges);
    hist.at<float>(0) -= float(alreadyMasked);
    cv::Point peak;
    cv::minMaxLoc(hist, nullptr, nullptr, nullptr, &peak);
    const int peakIdx = peak.y;
    double amountMasked = 0, currentLevel = 0, lastLevel = DBL_MAX;
    int start = peakIdx, end = circular ? (peakIdx - 1 + kBins) % kBins : peakIdx - 1;
    bool up = true;
    if (circular) {
        while (amountMasked < amountToMask) {
            if (up) {
                end = (end + 1) % kBins;
                currentLevel = hist.at<float>(end);
            } else {
                start = (start - 1 + kBins) % kBins;
                currentLevel = hist.at<float>(start);
            }
            amountMasked += currentLevel;
            if (currentLevel <= lastLevel) {
                lastLevel = currentLevel;
                up = !up;
            }
        }
    } else {
        while (amountMasked < amountToMask && (start > 0 || end < kBins - 1)) {
            if (up) {
                if (end < kBins - 1) {
                    ++end;
                    currentLevel = hist.at<float>(end);
                    amountMasked += currentLevel;
                    if (currentLevel < lastLevel && start > 0) {
                        lastLevel = currentLevel;
                        up = !up;
                    }
                } else {
                    currentLevel = 0;
                    up = false;
                }
            } else {
                if (start > 0) {
                    --start;
                    currentLevel = hist.at<float>(start);
                    amountMasked += currentLevel;
                    if (currentLevel < lastLevel && end < kBins - 1) {
                        lastLevel = currentLevel;
                        up = !up;
                    }
                } else {
                    currentLevel = 0;
                    up = true;
                }
            }
        }
    }
    if (amountToMask == 0) return { -1, -1 };
    return { start, end };
}

} // namespace

void JPStageRegistry::addFilterStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "BlurGaussian", "Image Processing", "Performs gaussian blurring on the working image.",
                      { P { "kernel-size", Kind::Integer, "3", "Width and height of the blurring kernel. Should be and odd number greater than or equal to 3" },
                        P { "property-name", Kind::Text, "BlurGaussian", "Name of the property through which OpenPnP controls this stage. Use \"BlurGaussian\" for standard control." } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const int k = int(std::lround(p.overridden(s, "kernel-size", oddKernel(s.integer("kernel-size")),
                                                                     s.text("property-name") + ".kernelSize"))) | 1;
                          cv::Mat& mat = p.workingImage();
                          cv::GaussianBlur(mat, mat, cv::Size(k, k), 0);
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "BlurMedian", "Image Processing", "Performs median blurring on the working image.",
                      { P { "kernel-size", Kind::Integer, "3", "Width and height of the blurring kernel. Should be and odd number greater than or equal to 3" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          cv::Mat& mat = p.workingImage();
                          cv::medianBlur(mat, mat, oddKernel(s.integer("kernel-size")));
                          return Output {};
                      } });
    {
        std::vector<std::string> codes;
        for (const ColorCode& c : kColorCodes) codes.push_back(c.name);
        types.push_back({ std::string(kStages) + "ConvertColor", "Color Space",
                          "Converts the internal representation of an image from one color space to another.  Note that the "
                          "conversions (other than to/from gray) do not alter the colors in the image but rather change the "
                          "underlying numerical represention of the colors such that the preceived colors in the new color "
                          "space are the same as those in the old color space.",
                          { P { "conversion", Kind::Choice, "Bgr2Gray", "Selects the from/to color space conversion.", codes } },
                          [](JPPipeline& p, const JPPipelineStage& s) {
                              const std::string name = s.text("conversion");
                              for (const ColorCode& c : kColorCodes)
                                  if (name == c.name) {
                                      cv::Mat& mat = p.workingImage();
                                      cv::cvtColor(mat, mat, c.code);
                                      p.setWorkingColorSpace(c.resulting);
                                      return Output {};
                                  }
                              throw std::runtime_error("Unknown conversion " + name);
                          } });
    }
    types.push_back({ std::string(kStages) + "Threshold", "", "",
                      { P { "threshold", Kind::Integer, "100", "" }, P { "auto", Kind::Flag, "false", "" },
                        P { "invert", Kind::Flag, "false", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          cv::Mat& mat = p.workingImage();
                          int type = s.flag("invert") ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY;
                          if (s.flag("auto")) type |= cv::THRESH_OTSU;
                          cv::threshold(mat, mat, s.integer("threshold"), 255, type);
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "ThresholdAdaptive", "Image Processing", "Performs adaptive thresholding on the working image.",
                      { P { "adaptive-method", Kind::Choice, "Mean",
                            "Adaptive thresholding algorithm to use: 'Mean' is a mean of the (blockSize x blockSize) neighborhood of a pixel minus cParm. 'Gaussian' is a weighted sum (cross-correlation with a Gaussian window) of the (blockSize x blockSize) neighborhood of a pixel minus cParm",
                            { "Mean", "Gaussian" } },
                        P { "invert", Kind::Flag, "false", "Thresholding type that must be either binary (default) or inverted binary" },
                        P { "block-size", Kind::Integer, "127", "Size of a pixel neighborhood that is used to calculate a threshold value for the pixel. Should be and odd number greater than or equal to 3" },
                        P { "c-parm", Kind::Integer, "80", "Constant subtracted from the mean or weighted mean. Can take negative values too." } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          cv::Mat& mat = p.workingImage();
                          cv::adaptiveThreshold(mat, mat, 255,
                                                s.text("adaptive-method") == "Gaussian" ? cv::ADAPTIVE_THRESH_GAUSSIAN_C
                                                                                        : cv::ADAPTIVE_THRESH_MEAN_C,
                                                s.flag("invert") ? cv::THRESH_BINARY_INV : cv::THRESH_BINARY,
                                                oddKernel(s.integer("block-size")), s.integer("c-parm"));
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "MaskCircle", "",
                      "Mask everything in the working image outside of a circle centered at the center of the image with the specified diameter.",
                      { P { "diameter", Kind::Integer, "100", "The diameter of the circle to mask. Use a negative value to invert the mask." },
                        P { "property-name", Kind::Text, "MaskCircle", "Name of the property through which OpenPnP controls this stage. Use \"MaskCircle\" for standard control." } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const cv::Mat& mat = p.workingImage();
                          const std::string control = s.text("property-name");
                          const int diameter = int(std::lround(p.overridden(s, "diameter", s.integer("diameter"), control + ".diameter")));
                          const cv::Point2d center = p.overriddenPoint(s, "center", { mat.cols * 0.5, mat.rows * 0.5 }, control + ".center");
                          cv::Mat mask(mat.size(), CV_8UC1, cv::Scalar(0));
                          if (diameter != 0) cv::circle(mask, center, std::abs(diameter) / 2, cv::Scalar(255), -1);
                          if (diameter < 0) cv::bitwise_not(mask, mask);
                          Output out;
                          out.image = masked(mat, mask);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "MaskHsv", "",
                      "Mask color from an image based on the HSV color space. Pixels that fall between (hueMin, saturationMin, valueMin) and (hueMax, saturationMax, valueMax) are set to black in the output image. This stage expects the input to be in HSV_FULL format, so you should do a ConvertColor with Bgr2HsvFull before this stage and ConvertColor Hsv2BgrFull after. These are not applied internally as to not complicate the use of multiple instances of this stage in series. Note that this stage can be used with any 3 channel, 8 bit per channel color space. The order of the filtered channels is hue, saturation, value, but you can use these ranges for other channels.",
                      { P { "auto", Kind::Flag, "false", "Sets each channel's min and max limits such that the pixels falling in and around the channel's histogram peak bin are masked.  The number of histogram bins that get masked is controlled by the value of the fractionToMask parameter." },
                        P { "fraction-to-mask", Kind::Number, "0.0", "When the auto flag is set, the min and max limits are set so that the fraction of pixels in the image that get masked is approximately equal to this value.  The pixels with the most commonly occurring colors are masked first.   Valid range is from 0.0 to 1.0 (inclusive)." },
                        P { "hue-min", Kind::Integer, "31", "First hue to be masked.  Note hues range from 0 to 255 (inclusive) but in a circular fashion so that 255 is directly adjacent to 0 (as 359 degrees is adjacent to 0 degrees).  To mask hues that cross the 255-0 boundary, set hueMin greater than hueMax.  As a rough guide, yellows fall in the range 21 to 64, greens 64 to 107, cyans 107 to 149, blues 149 to 192, magentas 192 to 235, and reds 235 to 21." },
                        P { "hue-max", Kind::Integer, "116", "Last hue to be masked.  Note hues range from 0 to 255 (inclusive) but in a circular fashion so that 255 is directly adjacent to 0 (as 359 degrees is adjacent to 0 degrees).  To mask hues that cross the 255-0 boundary, set hueMin greater than hueMax.  As a rough guide, yellows fall in the range 21 to 64, greens 64 to 107, cyans 107 to 149, blues 149 to 192, magentas 192 to 235, and reds 235 to 21." },
                        P { "saturation-min", Kind::Integer, "0", "Minimum saturation to be masked.  Note saturations range from 0 to 255 (inclusive). Setting saturationMin greater than saturationMax will result in no pixels being masked." },
                        P { "saturation-max", Kind::Integer, "255", "Maximum saturation to be masked.  Note saturations range from 0 to 255 (inclusive). Setting saturationMax less than saturationMin will result in no pixels being masked." },
                        P { "value-min", Kind::Integer, "0", "Minimum value to be masked.  Note values range from 0 to 255 (inclusive). Setting valueMin greater than valueMax will result in no pixels being masked." },
                        P { "value-max", Kind::Integer, "255", "Maximum value to be masked.  Note values range from 0 to 255 (inclusive). Setting valueMax less than valueMin will result in no pixels being masked." },
                        P { "soft-edge", Kind::Integer, "0", "Soft edge of the HSV mask bounding box. If not 0, the mask is computed and applied with gradual impact, creating a more natural result." },
                        P { "soft-factor", Kind::Number, "1.0", "Soft factor, i.e. how strongly the mask is applied, from 0 ... 1.0." },
                        P { "invert", Kind::Flag, "", "Inverts the selection of pixels to mask." },
                        P { "binary-mask", Kind::Flag, "false", "If set, the mask is returned directly as a grayscale image with the masked area black, the unmasked white. Otherwise the masked area is blackened in the source image." },
                        P { "property-name", Kind::Text, "MaskHsv", "Name of the property through which OpenPnP controls this stage. Use \"MaskHsv\" for standard control." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          // A stage from before the invert flag: inverted when its hues wrapped round.
                          if (s.text("invert").empty()) {
                              if (s.integer("hue-min") > s.integer("hue-max")) {
                                  const std::string lo = s.text("hue-min");
                                  s.set("hue-min", s.text("hue-max"));
                                  s.set("hue-max", lo);
                                  s.set("invert", "true");
                              } else {
                                  s.set("invert", "false");
                              }
                          }
                          const bool invert = s.flag("invert");
                          const double softFactor = std::max(0.0, s.number("soft-factor"));
                          const cv::Mat& mat = p.workingImage();
                          if (s.flag("auto")) {
                              // Black pixels (value 0) are masked already.
                              cv::Mat lit, working(mat.size(), mat.type(), cv::Scalar::all(0));
                              cv::inRange(mat, cv::Scalar(0, 0, 1), cv::Scalar(255, 255, 255), lit);
                              mat.copyTo(working, lit);
                              const double already = double(mat.total()) - cv::countNonZero(lit);
                              const double amount = s.number("fraction-to-mask") * (double(mat.total()) - already);
                              const char* channel[3][2] = { { "hue-min", "hue-max" }, { "saturation-min", "saturation-max" },
                                                            { "value-min", "value-max" } };
                              for (int c = 0; c < 3; ++c) {
                                  const auto [lo, hi] = autoLimits(working, c, already, amount, c == 0);
                                  s.set(channel[c][1], std::to_string(hi));
                                  s.set(channel[c][0], std::to_string(lo));
                              }
                          }
                          const std::string control = s.text("property-name");
                          auto limit = [&](const char* attribute, const char* name) {
                              return int(std::lround(p.overridden(s, attribute, s.integer(attribute), control + "." + name)));
                          };
                          const int hueMin = limit("hue-min", "hueMin"), hueMax = limit("hue-max", "hueMax");
                          const int satMin = limit("saturation-min", "saturationMin"), satMax = limit("saturation-max", "saturationMax");
                          const int valMin = limit("value-min", "valueMin"), valMax = limit("value-max", "valueMax");
                          const int softEdge = s.integer("soft-edge");
                          const bool binary = s.flag("binary-mask");
                          Output out;
                          if (softEdge > 0 || softFactor < 1) {
                              // Masked gradually: by the distance from the box of limits.
                              const double complement = 1 - softFactor;
                              const uchar floorValue = uchar(complement * 255.999);
                              const bool factorOne = softFactor == 1.0;
                              const bool hueAll = hueMin == 0 && hueMax == 255;
                              const int satMinE = satMin == 0 ? satMin - softEdge : satMin, satMaxE = satMax == 255 ? satMax + softEdge : satMax;
                              const int valMinE = valMin == 0 ? valMin - softEdge : valMin, valMaxE = valMax == 255 ? valMax + softEdge : valMax;
                              const int hueMid = hueAll ? 128 : hueMin <= hueMax ? (hueMin + hueMax) / 2 : (((256 + hueMin + hueMax) / 2) & 0xFF);
                              const int hueSpan = hueAll ? 128 : std::max(0, (((hueMax - hueMin) & 0xFF) + 1) / 2 - softEdge / 2);
                              const int satMid = (satMinE + satMaxE) / 2, satSpan = std::max(0, (satMaxE - satMinE + 1) / 2 - softEdge / 2);
                              const int valMid = (valMinE + valMaxE) / 2, valSpan = std::max(0, (valMaxE - valMinE + 1) / 2 - softEdge / 2);
                              const int softSq = softEdge * softEdge;
                              cv::Mat result = mat.clone();
                              cv::Mat mask(mat.size(), CV_8UC1, cv::Scalar(255));
                              for (int y = 0; y < mat.rows; ++y) {
                                  uchar* px = result.ptr<uchar>(y);
                                  uchar* m = mask.ptr<uchar>(y);
                                  for (int x = 0; x < mat.cols; ++x, px += mat.channels()) {
                                      int hue = px[0], sat = px[1], val = px[2];
                                      const int hueDS = std::abs(hue - hueMid);
                                      const int hueD = std::max(0, (hueDS < 128 ? hueDS : 256 - hueDS) - hueSpan);
                                      const int satD = std::max(0, std::abs(sat - satMid) - satSpan);
                                      const int valD = std::max(0, std::abs(val - valMid) - valSpan);
                                      const int sq = hueD * hueD + satD * satD + valD * valD;
                                      if (sq >= softSq || sq == 0) {
                                          if ((sq == 0) != invert) {
                                              if (factorOne) hue = sat = val = 0;
                                              else val = uchar(val * complement);
                                              m[x] = floorValue;
                                          }
                                      } else {
                                          double d = std::cos(CV_PI * std::sqrt(double(sq)) / softEdge) * -0.5 + 0.5;
                                          if (invert) d = 1 - d;
                                          const double f = softFactor * d + complement;
                                          val = int(val * f);
                                          m[x] = uchar(255.999 * f);
                                      }
                                      px[0] = uchar(hue);
                                      px[1] = uchar(sat);
                                      px[2] = uchar(val);
                                  }
                              }
                              if (binary) {
                                  out.image = mask;
                                  out.colorSpace = "Gray";
                              } else {
                                  out.image = result;
                              }
                              return out;
                          }
                          // The range (hue wrapping round past 255 when hueMin > hueMax).
                          cv::Mat mask;
                          if (hueMin <= hueMax) {
                              cv::inRange(mat, cv::Scalar(hueMin, satMin, valMin), cv::Scalar(hueMax, satMax, valMax), mask);
                          } else {
                              cv::Mat mask2;
                              cv::inRange(mat, cv::Scalar(hueMin, satMin, valMin), cv::Scalar(255, satMax, valMax), mask);
                              cv::inRange(mat, cv::Scalar(0, satMin, valMin), cv::Scalar(hueMax, satMax, valMax), mask2);
                              cv::bitwise_or(mask, mask2, mask);
                          }
                          // What is copied is what is not masked.
                          if (!invert) cv::bitwise_not(mask, mask);
                          if (binary) {
                              out.image = mask;
                              out.colorSpace = "Gray";
                          } else {
                              out.image = masked(mat, mask);
                          }
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "MaskRectangle", "", "",
                      { P { "width", Kind::Integer, "100", "" }, P { "height", Kind::Integer, "100", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const cv::Mat& mat = p.workingImage();
                          const int w = s.integer("width"), h = s.integer("height");
                          cv::Mat mask(mat.size(), CV_8UC1, cv::Scalar(0));
                          if (w != 0 && h != 0)
                              cv::rectangle(mask, cv::Point(mat.cols / 2 - w / 2, mat.rows / 2 - h / 2),
                                            cv::Point(mat.cols / 2 + w / 2, mat.rows / 2 + h / 2), cv::Scalar(255), -1);
                          if (w * h < 0) cv::bitwise_not(mask, mask);
                          Output out;
                          out.image = masked(mat, mask);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "Normalize", "",
                      "On color images it does RGB Max algorithm, removes shadow and looses color information.", {},
                      [](JPPipeline& p, const JPPipelineStage&) {
                          cv::Mat& mat = p.workingImage();
                          if (mat.channels() == 1) {
                              cv::normalize(mat, mat, 0, 255, cv::NORM_MINMAX);
                          } else if (mat.channels() >= 3) {
                              // Each pixel's channels over their sum.
                              cv::Mat d;
                              mat.convertTo(d, CV_64F);
                              for (int y = 0; y < d.rows; ++y) {
                                  double* row = d.ptr<double>(y);
                                  for (int x = 0; x < d.cols; ++x) {
                                      double* px = row + x * d.channels();
                                      const double sum = px[0] + px[1] + px[2];
                                      if (sum != 0)
                                          for (int c = 0; c < 3; ++c) px[c] = px[c] / sum * 255;
                                  }
                              }
                              d.convertTo(mat, mat.type());
                          }
                          Output out;
                          out.image = mat;
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "HistogramEqualize", "",
                      "Applies histogram equalization to the selected channels of the image.  For gray scale images this will "
                      "increase the image contrast.  For color images, the results will vary depending on the image format and "
                      "channels selected for equalization.  Generally applying histogram equalization to a color image will "
                      "result in a false color image; however, contrast enhancement can be achieved on HSV formats by applying "
                      "equalization to only the third channel (V).",
                      { P { "channels-to-equalize", Kind::Choice, "All",
                            "Selects which channel(s) of the image to equalize.  This setting has no effect on single channel (gray scale) images.",
                            { "First", "Second", "Third", "FirstAndSecond", "FirstAndThird", "SecondAndThird", "All" } } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          static const std::pair<const char*, int> codes[] = { { "First", 1 }, { "Second", 2 }, { "Third", 4 },
                                                                               { "FirstAndSecond", 3 }, { "FirstAndThird", 5 },
                                                                               { "SecondAndThird", 6 }, { "All", 7 } };
                          int code = 7;
                          for (const auto& [n, c] : codes)
                              if (s.text("channels-to-equalize") == n) code = c;
                          cv::Mat& mat = p.workingImage();
                          const int n = mat.channels();
                          for (int i = 0; i < n; ++i)
                              if (n == 1 || (code >> i) % 2 == 1) {
                                  cv::Mat ch;
                                  cv::extractChannel(mat, ch, i);
                                  cv::equalizeHist(ch, ch);
                                  cv::insertChannel(ch, mat, i);
                              }
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "HistogramEqualizeAdaptive", "",
                      "Applies contrast limited adaptive histogram equalization (CLAHE) to the selected channels of the image.  For gray scale images this will increase the image contrast.  For color images, the results will vary depending on the image format and channels selected for equalization.  Generally applying histogram equalization to a color image will result in a false color image; however, contrast enhancement can be achieved on HSV formats by applying equalization to only the third channel (V).",
                      { P { "channels-to-equalize", Kind::Choice, "All",
                            "Selects which channel(s) of the image to equalize.  This setting has no effect on single channel (gray scale) images.",
                            { "First", "Second", "Third", "FirstAndSecond", "FirstAndThird", "SecondAndThird", "All" } },
                        P { "clip-limit", Kind::Number, "2.0", "The threshold for contrast limiting.  Lower values will limit the amount of contrast enhancement while higher values will allow for more enhancement but at the risk of increasing noise in homogeneous regions of the image." },
                        P { "number-of-tile-rows", Kind::Integer, "10", "The number of tile rows into which the image is partitioned." },
                        P { "number-of-tile-cols", Kind::Integer, "16", "The number of tile columns into which the image is partitioned." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          static const std::pair<const char*, int> codes[] = { { "First", 1 }, { "Second", 2 }, { "Third", 4 },
                                                                               { "FirstAndSecond", 3 }, { "FirstAndThird", 5 },
                                                                               { "SecondAndThird", 6 }, { "All", 7 } };
                          int code = 7;
                          for (const auto& [n, c] : codes)
                              if (s.text("channels-to-equalize") == n) code = c;
                          auto clahe = cv::createCLAHE(s.number("clip-limit"),
                                                       cv::Size(s.integer("number-of-tile-rows"), s.integer("number-of-tile-cols")));
                          cv::Mat& mat = p.workingImage();
                          const int n = mat.channels();
                          for (int i = 0; i < n; ++i)
                              if (n == 1 || (code >> i) % 2 == 1) {
                                  cv::Mat ch;
                                  cv::extractChannel(mat, ch, i);
                                  clahe->apply(ch, ch);
                                  cv::insertChannel(ch, mat, i);
                              }
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "GrabCut", "", "",
                      { P { "side-square", Kind::Integer, "50", "" }, P { "back-ground-origin-x", Kind::Integer, "50", "" },
                        P { "back-ground-origin-y", Kind::Integer, "50", "" } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          cv::Mat& mat = p.workingImage();
                          const int side = s.integer("side-square"), ox = s.integer("back-ground-origin-x"), oy = s.integer("back-ground-origin-y");
                          const cv::Rect rect(cv::Point(ox - side, oy - side), cv::Point(ox + side, oy + side));
                          cv::Mat mask, bg, fg;
                          cv::grabCut(mat, mask, rect, bg, fg, 1, cv::GC_INIT_WITH_RECT);
                          // The foreground, sure and likely.
                          cv::Mat fgMask = mask == cv::GC_FGD, pfgMask = mask == cv::GC_PR_FGD;
                          cv::Mat fgImage(mat.size(), mat.type(), cv::Scalar::all(0)), pfgImage(mat.size(), mat.type(), cv::Scalar::all(0));
                          mat.copyTo(fgImage, fgMask);
                          mat.copyTo(pfgImage, pfgMask);
                          cv::bitwise_or(fgImage, pfgImage, mat);
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "DetectEdgesCanny", "", "",
                      { P { "threshold1", Kind::Number, "40.0", "" }, P { "threshold2", Kind::Number, "180.0", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          cv::Mat& mat = p.workingImage();
                          cv::Canny(mat, mat, s.number("threshold1"), s.number("threshold2"));
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "DetectEdgesLaplacian", "", "", {},
                      [](JPPipeline& p, const JPPipelineStage&) {
                          cv::Mat& mat = p.workingImage();
                          cv::Laplacian(mat, mat, mat.depth());
                          return Output {};
                      } });
    types.push_back({ std::string(kStages) + "DetectEdgesRobertsCross", "", "", {},
                      [](JPPipeline& p, const JPPipelineStage&) {
                          const cv::Mat& mat = p.workingImage();
                          cv::Mat kernel = (cv::Mat_<float>(2, 2) << 0, 1, -1, 0);
                          cv::Mat r1, r2, out;
                          cv::filter2D(mat, r1, CV_32F, kernel);
                          cv::convertScaleAbs(r1, r1);
                          kernel = (cv::Mat_<float>(2, 2) << 1, 0, 0, -1);
                          cv::filter2D(mat, r2, CV_32F, kernel);
                          cv::convertScaleAbs(r2, r2);
                          cv::add(r1, r2, out);
                          Output o;
                          o.image = out;
                          return o;
                      } });
    types.push_back({ std::string(kStages) + "Rotate", "", "", { P { "degrees", Kind::Number, "0.0", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          const double degrees = s.number("degrees");
                          const cv::Mat& mat = p.workingImage();
                          if (degrees == 0) return Output {};
                          const cv::Point2f center(float(mat.cols) / 2, float(mat.rows) / 2);
                          cv::Mat map = cv::getRotationMatrix2D(center, degrees, 1.0);
                          const cv::Rect box = cv::RotatedRect(center, mat.size(), float(degrees)).boundingRect();
                          map.at<double>(0, 2) += box.width / 2.0 - center.x;
                          map.at<double>(1, 2) += box.height / 2.0 - center.y;
                          Output out;
                          cv::warpAffine(mat, out.image, map, box.size(), cv::INTER_LINEAR);
                          return out;
                      } });
    types.push_back({ std::string(kStages) + "SetColor", "", "Set all pixels of the working image to the specified color.",
                      { P { "color", Kind::Color, "0,0,0,255", "" } },
                      [](JPPipeline& p, const JPPipelineStage& s) {
                          p.workingImage().setTo(s.color("color"));
                          return Output {};
                      } });
}

} // inline namespace jf
