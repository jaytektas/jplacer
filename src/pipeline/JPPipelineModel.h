// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <opencv2/core.hpp>

#include <cmath>
#include <string>
#include <variant>
#include <vector>

inline namespace jf {

// What a pipeline stage found (OpenPnP's Result.model): nothing, a rotated
// rectangle or several, circles, key points, contours, lines, template
// matches, points, a number, a text, an AffineWarp's transform (picture to
// warped picture), text read, or the reason it failed.
struct JPPipelineModel {
    // A circle; DetectCircularSymmetry's carry their symmetry score (else NaN).
    struct Circle {
        double x = 0, y = 0, diameter = 0;
        double score = NAN;
    };
    struct TemplateMatch {
        double x = 0, y = 0, width = 0, height = 0, score = 0;
    };
    struct Line {
        cv::Point2d a, b;
    };
    struct Failure {
        std::string message;
    };
    // SimpleOcr's text (lines apart by '\n'), its characters and their scores summed.
    struct Ocr {
        std::string text;
        int         numChars = 0;
        double      overallScore = 0;
    };
    using Contours = std::vector<std::vector<cv::Point>>;

    std::variant<std::monostate, cv::RotatedRect, std::vector<cv::RotatedRect>, std::vector<Circle>,
                 std::vector<cv::KeyPoint>, Contours, std::vector<Line>, std::vector<TemplateMatch>,
                 std::vector<cv::Point2d>, cv::Point2d, double, std::string, Failure, cv::KeyPoint, Circle, TemplateMatch, cv::Matx23d, Ocr>
        value;

    bool empty() const { return std::holds_alternative<std::monostate>(value); }
    const Failure* failure() const { return std::get_if<Failure>(&value); }
    // As OpenPnP's editor shows it (its toString).
    std::string describe() const;
    // Its kind, for "returned a … but expected a …".
    std::string kind() const;
};

} // inline namespace jf
