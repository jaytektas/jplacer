// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's matching and blob stages on a picture of three bright discs: a
// disc's picture, written by one pipeline and read by the next as the
// template, is matched at each disc (best first, scores normalised to the
// best); a reach around the centre keeps only the near one; the blob
// detector finds the three; local maxima are each the top of their 3 x 3.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPXmlReader.h"
#include "pipeline/JPPipeline.h"
#include "pipeline/JPStageUtil.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>

using namespace jf;

namespace {

const cv::Point kDiscs[] = { { 320, 240 }, { 120, 100 }, { 520, 380 } };

JPPipeline make(const std::string& xml) {
    JPXmlElement root;
    std::string error;
    assert(JPXmlReader::parse(xml, root, error));
    return JPPipeline::fromXml(root);
}

void camera(JPPipeline& p, bool justOne) {
    p.context().capture = [justOne](const std::string&, const std::string&, cv::Mat& bgr, std::string&) {
        bgr = cv::Mat(justOne ? 60 : 480, justOne ? 60 : 640, CV_8UC3, cv::Scalar(10, 10, 10));
        if (justOne)
            cv::circle(bgr, { 30, 30 }, 20, cv::Scalar(240, 240, 240), cv::FILLED);
        else
            for (const cv::Point& c : kDiscs) cv::circle(bgr, c, 20, cv::Scalar(240, 240, 240), cv::FILLED);
        return true;
    };
}

} // namespace

int main() {
    const std::string file = (std::filesystem::temp_directory_path() / "jplacer_test_disc.png").string();
    const std::string S = "org.openpnp.vision.pipeline.stages.";

    // A disc's picture, written.
    JPPipeline writer = make("<cv-pipeline><stages>"
                             "<cv-stage class=\"" + S + "ImageCapture\" name=\"0\" enabled=\"true\" default-light=\"true\" settle-option=\"Settle\" count=\"1\"/>"
                             "<cv-stage class=\"" + S + "ConvertColor\" name=\"1\" enabled=\"true\" conversion=\"Bgr2Gray\"/>"
                             "<cv-stage class=\"" + S + "ImageWrite\" name=\"2\" enabled=\"true\" file=\"" + file + "\"/>"
                             "</stages></cv-pipeline>");
    camera(writer, true);
    std::string why;
    assert(writer.process(why) && std::filesystem::exists(file));

    // Read back in colour, as OpenPnP's imread: matched on the colour capture,
    // at each disc, as the top-left corner of the template.
    const std::string match = "<cv-pipeline><stages>"
                              "<cv-stage class=\"" + S + "ImageCapture\" name=\"0\" enabled=\"true\" default-light=\"true\" settle-option=\"Settle\" count=\"1\"/>"
                              "<cv-stage class=\"" + S + "ConvertColor\" name=\"1\" enabled=\"true\" conversion=\"Bgr2Gray\"/>"
                              "<cv-stage class=\"" + S + "ImageRead\" name=\"templ\" enabled=\"true\" file=\"" + file + "\" color-space=\"Bgr\" handle-as-captured=\"false\"/>"
                              "<cv-stage class=\"" + S + "ImageRecall\" name=\"3\" enabled=\"true\" image-stage-name=\"0\"/>"
                              "<cv-stage class=\"" + S + "MatchTemplate\" name=\"match\" enabled=\"true\" template-stage-name=\"templ\" threshold=\"0.7\" corr=\"0.85\" normalize=\"true\" max-distance=\"MAXD\" property-name=\"\"/>"
                              "<cv-stage class=\"" + S + "ImageRecall\" name=\"5\" enabled=\"true\" image-stage-name=\"1\"/>"
                              "<cv-stage class=\"" + S + "SimpleBlobDetector\" name=\"blobs\" enabled=\"true\" threshold-step=\"10.0\" threshold-min=\"50.0\" threshold-max=\"220.0\" repeatability=\"2\" dist-between-blobs=\"10.0\" color=\"true\" color-value=\"255.0\" area=\"true\" area-min=\"25.0\" area-max=\"5000.0\" circularity=\"false\" circularity-min=\"0.8\" circularity-max=\"-1.0\" inertia=\"true\" inertia-ratio-min=\"0.1\" inertia-ratio-max=\"-1.0\" convexity=\"true\" convexity-min=\"0.95\" convexity-max=\"-1.0\" property-name=\"SimpleBlobDetector\"/>"
                              "</stages></cv-pipeline>";
    auto withReach = [&](const std::string& d) {
        std::string x = match;
        x.replace(x.find("MAXD"), 4, d);
        return x;
    };
    JPPipeline all = make(withReach("10000"));
    camera(all, false);
    assert(all.process(why));
    const auto* matches = std::get_if<std::vector<JPPipelineModel::TemplateMatch>>(&all.expectedResult("match").model.value);
    assert(matches && matches->size() == 3);
    for (const auto& m : *matches) {
        bool atDisc = false;
        for (const cv::Point& c : kDiscs) atDisc |= std::abs(m.x + 30 - c.x) <= 1 && std::abs(m.y + 30 - c.y) <= 1;
        assert(atDisc && m.width == 60 && m.height == 60);
    }
    assert(std::abs(matches->front().score - 1.0) < 1e-6);
    for (size_t i = 1; i < matches->size(); ++i) assert((*matches)[i - 1].score >= (*matches)[i].score);

    // Three blobs, at the discs.
    const auto* blobs = std::get_if<std::vector<cv::KeyPoint>>(&all.expectedResult("blobs").model.value);
    assert(blobs && blobs->size() == 3);
    for (const cv::KeyPoint& k : *blobs) {
        bool atDisc = false;
        for (const cv::Point& c : kDiscs) atDisc |= std::hypot(k.pt.x - c.x, k.pt.y - c.y) < 2;
        assert(atDisc);
    }

    // Reach 50 px from the centre of the result: only the middle disc.
    JPPipeline near = make(withReach("50"));
    camera(near, false);
    assert(near.process(why));
    matches = std::get_if<std::vector<JPPipelineModel::TemplateMatch>>(&near.expectedResult("match").model.value);
    assert(matches && matches->size() == 1 && std::abs(matches->front().x + 30 - 320) <= 1);

    // Local maxima: a plateau-free peak each, the edge and the range respected.
    cv::Mat m(5, 6, CV_32F, cv::Scalar(0));
    m.at<float>(1, 1) = 0.9f;
    m.at<float>(3, 4) = 0.8f;
    m.at<float>(2, 5) = 0.5f;
    auto peaks = JPStageUtil::matMaxima(m, 0.6, 1.0);
    assert(peaks.size() == 2 && peaks[0] == cv::Point(1, 1) && peaks[1] == cv::Point(4, 3));

    std::filesystem::remove(file);
    return 0;
}
