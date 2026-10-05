// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// SimpleOcr reads two lines of part marking drawn in its font, the gap
// between words a space; with the "[Barcode]" font it reads a QR code; an
// empty alphabet set by the caller turns it off. ActuatorWrite sets the
// named actuator to its value (as older files wrote it, too), and says when
// there is no such actuator; a machine that fails stops the pipeline.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPXmlReader.h"
#include "pipeline/JPPipeline.h"

#include <j/graphics/FontEngine.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include <fstream>

using namespace jf;

namespace {

const std::string S = "org.openpnp.vision.pipeline.stages.";

JPPipeline make(const std::string& stages) {
    JPXmlElement root;
    std::string error;
    assert(JPXmlReader::parse("<cv-pipeline><stages>" + stages + "</stages></cv-pipeline>", root, error));
    return JPPipeline::fromXml(root);
}

// Black text on white in Liberation Mono at `pixels`, a line apart by the font's height.
cv::Mat marking(const std::vector<std::string>& lines, int pixels) {
    const std::string path = jResolveFontFace("Liberation Mono", false, false);
    assert(!path.empty());
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    stbtt_fontinfo info;
    assert(stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)));
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, float(pixels));
    int a, d, gap;
    stbtt_GetFontVMetrics(&info, &a, &d, &gap);
    const int ascent = int(0.95f + a * scale), height = ascent + int(0.95f - d * scale);
    cv::Mat img(height * int(lines.size() + 2), 300, CV_8UC1, cv::Scalar(255));
    for (size_t l = 0; l < lines.size(); ++l) {
        int pen = 20;
        for (const char ch : lines[l]) {
            int x0, y0, x1, y1, advance, bearing;
            stbtt_GetCodepointBitmapBox(&info, ch, scale, scale, &x0, &y0, &x1, &y1);
            std::vector<unsigned char> cov(size_t(std::max(1, (x1 - x0) * (y1 - y0))));
            if (x1 > x0 && y1 > y0) stbtt_MakeCodepointBitmap(&info, cov.data(), x1 - x0, y1 - y0, x1 - x0, scale, scale, ch);
            const int top = height * int(l + 1) + ascent;
            for (int r = 0; r < y1 - y0; ++r)
                for (int c = 0; c < x1 - x0; ++c) img.at<uchar>(top + y0 + r, pen + x0 + c) = uchar(255 - cov[size_t(r * (x1 - x0) + c)]);
            stbtt_GetCodepointHMetrics(&info, ch, &advance, &bearing);
            pen += int(0.5f + advance * scale);
        }
    }
    return img;
}

void camera(JPPipeline& p, const cv::Mat& picture) {
    p.context().pixelsPerMmX = p.context().pixelsPerMmY = 20;
    p.context().capture = [picture](const std::string&, const std::string&, cv::Mat& bgr, std::string&) {
        cv::cvtColor(picture, bgr, cv::COLOR_GRAY2BGR);
        return true;
    };
}

} // namespace

int main() {
    std::string why;
    const std::string capture = "<cv-stage class=\"" + S + "ImageCapture\" name=\"0\" enabled=\"true\" default-light=\"true\" settle-option=\"Settle\" count=\"1\"/>"
                                "<cv-stage class=\"" + S + "ConvertColor\" name=\"1\" enabled=\"true\" conversion=\"Bgr2Gray\"/>";
    // 3 pt at 20 px/mm: 21 px.
    JPPipeline ocr = make(capture + "<cv-stage class=\"" + S + "SimpleOcr\" name=\"ocr\" enabled=\"true\" alphabet=\"0123456789.-+_RCLDQYXJIVAFH%GMKkmuµnp\" font-name=\"Liberation Mono\" font-size-pt=\"3.0\" font-max-pixel-size=\"20\" auto-detect-size=\"false\" threshold=\"0.75\" draw-style=\"OverOriginalImage\" debug=\"false\" property-name=\"SimpleOcr\"/>");
    camera(ocr, marking({ "R10K 5%", "0805" }, 21));
    assert(ocr.process(why));
    const JPPipeline::Result& read = ocr.expectedResult("ocr");
    const auto* text = std::get_if<JPPipelineModel::Ocr>(&read.model.value);
    assert(text && text->text == "R10K 5%\n0805" && text->numChars == 10 && text->overallScore > 7.5);
    // The marking drawn over the picture, in colour.
    assert(read.image.channels() == 3);

    // Turned off by the caller.
    ocr.setProperty("SimpleOcr.alphabet", JPPipelineValue { std::string() });
    assert(ocr.process(why) && ocr.result("ocr")->model.empty());

    // A QR code.
    cv::Mat code;
    cv::QRCodeEncoder::create()->encode("C0603-100nF", code);
    cv::resize(code, code, code.size() * 6, 0, 0, cv::INTER_NEAREST);
    cv::copyMakeBorder(code, code, 40, 40, 40, 40, cv::BORDER_CONSTANT, cv::Scalar(255));
    JPPipeline qr = make(capture + "<cv-stage class=\"" + S + "SimpleOcr\" name=\"ocr\" enabled=\"true\" font-name=\"[Barcode]\"/>");
    camera(qr, code);
    assert(qr.process(why));
    const auto* decoded = std::get_if<JPPipelineModel::Ocr>(&qr.expectedResult("ocr").model.value);
    assert(decoded && decoded->text == "C0603-100nF" && decoded->numChars == 11);

    // ActuatorWrite: the value written, then one from an older file.
    std::vector<std::pair<std::string, std::string>> writes;
    auto actuators = [&](JPPipeline& p, bool works) {
        p.context().actuatorExists = [](const std::string& n) { return n == "Light"; };
        p.context().actuate = [&writes, works](const std::string& n, const std::string& v, std::string& w) {
            if (!works) {
                w = "Machine not connected";
                return false;
            }
            writes.emplace_back(n, v);
            return true;
        };
    };
    JPPipeline aw = make("<cv-stage class=\"" + S + "ActuatorWrite\" name=\"a\" enabled=\"true\" actuator-name=\"Light\"><actuator-write-value class=\"java.lang.Double\">0.5</actuator-write-value></cv-stage>"
                         "<cv-stage class=\"" + S + "ActuatorWrite\" name=\"b\" enabled=\"true\" actuator-name=\"Light\" actuator-type=\"Boolean\" actuator-value=\"1.0\"/>"
                         "<cv-stage class=\"" + S + "ActuatorWrite\" name=\"c\" enabled=\"true\" actuator-name=\"Pump\"/>");
    actuators(aw, true);
    assert(aw.process(why));
    assert(writes.size() == 2 && writes[0] == std::make_pair(std::string("Light"), std::string("0.5")) && writes[1].second == "true");
    assert(aw.result("c")->model.failure()->message == "Actuator writing (CvStage operation) failed. Unable to find an actuator named Pump");
    JPPipeline down = make("<cv-stage class=\"" + S + "ActuatorWrite\" name=\"a\" enabled=\"true\" actuator-name=\"Light\"/>");
    actuators(down, false);
    assert(!down.process(why) && why == "Machine not connected");
    return 0;
}
