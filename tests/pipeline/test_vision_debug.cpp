// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Vision debugging (JPVisionDebug), OpenPnP's Debug-level vision pictures: off, a pipeline run leaves nothing;
// on, each run leaves a folder of its own under log/vision, named by when and what it was for, holding each
// stage's picture in order and stages.txt (each stage, its class and what it found), and the pipeline's
// ImageWriteDebug stage writes into OpenPnP's folder for it.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "openpnp/JPXmlReader.h"
#include "pipeline/JPPipeline.h"
#include "pipeline/JPVisionDebug.h"

#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

using namespace jf;
namespace fs = std::filesystem;

namespace {

const std::string S = "org.openpnp.vision.pipeline.stages.";

JPPipeline make(const std::string& stages) {
    JPXmlElement root;
    std::string error;
    assert(JPXmlReader::parse("<cv-pipeline><stages>" + stages + "</stages></cv-pipeline>", root, error));
    return JPPipeline::fromXml(root);
}

std::string stage(const std::string& cls, const std::string& name, const std::string& attributes) {
    return "<cv-stage class=\"" + S + cls + "\" name=\"" + name + "\" enabled=\"true\" " + attributes + "/>";
}

size_t entries(const fs::path& dir) {
    if (!fs::exists(dir)) return 0;
    return size_t(std::distance(fs::directory_iterator(dir), fs::directory_iterator()));
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("jplacer-vision-debug-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    JPPipeline p = make(stage("ImageCapture", "0", "default-light=\"true\" settle-option=\"Settle\" count=\"1\"")
                        + stage("ConvertColor", "gray", "conversion=\"Bgr2Gray\"")
                        + stage("ImageWriteDebug", "dbg", "prefix=\"look\" suffix=\".png\""));
    p.context().label = "bottom vision R1";
    p.context().capture = [](const std::string&, const std::string&, cv::Mat& bgr, std::string&) {
        bgr = cv::Mat(48, 64, CV_8UC3, cv::Scalar::all(10));
        cv::circle(bgr, cv::Point(32, 24), 8, cv::Scalar::all(240), cv::FILLED);
        return true;
    };
    std::string why;

    // Off: nothing written.
    JPVisionDebug::setDirectory("");
    assert(p.process(why));
    assert(!fs::exists(dir));

    // On: a folder for the run, its stages' pictures in order, what they found; ImageWriteDebug's picture.
    JPVisionDebug::setDirectory(dir.string());
    assert(JPVisionDebug::on());
    assert(p.process(why));
    const fs::path runs = dir / "log" / "vision";
    assert(entries(runs) == 1);
    const fs::path run = fs::directory_iterator(runs)->path();
    const std::string name = run.filename().string();
    assert(name.size() > 16 && name.find("_bottom_vision_R1") != std::string::npos);   // when, then what
    assert(fs::exists(run / "01_0.png") && fs::exists(run / "02_gray.png") && fs::exists(run / "03_dbg.png"));
    std::ifstream list(run / "stages.txt");
    std::stringstream text;
    text << list.rdbuf();
    assert(text.str().find("01 0 (" + S + "ImageCapture)") != std::string::npos);
    assert(text.str().find("02 gray (" + S + "ConvertColor)") != std::string::npos);
    const fs::path writes = dir / "org.openpnp.vision.pipeline.stages.ImageWriteDebug";
    assert(entries(writes) == 1 && fs::directory_iterator(writes)->path().filename().string().rfind("look", 0) == 0);

    // Off again: no more.
    JPVisionDebug::setDirectory("");
    assert(p.process(why));
    assert(entries(runs) == 1 && entries(writes) == 1);
    fs::remove_all(dir);
    return 0;
}
