// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's SimpleOcr: each character of an alphabet drawn in a font at the
// size the camera sees it, matched over the picture, the matches weighed
// against each other (overlaps lose, characters in a row win) and read off
// line by line; or, with the "[Barcode]" font, a QR code or barcode read.

#include "JPPipeline.h"
#include "JPStageRegistry.h"
#include "JPStageUtil.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>
#include <j/graphics/FontEngine.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <sstream>

inline namespace jf {

namespace {

using Kind = JPStageType::Kind;
using Output = JPStageType::Output;
using P = JPStageType::Property;
using Model = JPPipelineModel;
constexpr const char* kStages = "org.openpnp.vision.pipeline.stages.";
constexpr const char* kBarcodeFont = "[Barcode]";
// A family not installed is drawn in this one, as Java falls back to its Dialog font.
constexpr const char* kFallbackFamily = "DejaVu Sans";
// ZXing's HybridBinarizer looks at about this many pixels around each.
constexpr int kBinarizeBlock = 41;

std::vector<char32_t> codepoints(const std::string& s) {
    std::vector<char32_t> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        const int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        char32_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string utf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) out += { char(0xC0 | (cp >> 6)), char(0x80 | (cp & 0x3F)) };
    else if (cp < 0x10000) out += { char(0xE0 | (cp >> 12)), char(0x80 | ((cp >> 6) & 0x3F)), char(0x80 | (cp & 0x3F)) };
    else out += { char(0xF0 | (cp >> 18)), char(0x80 | ((cp >> 12) & 0x3F)), char(0x80 | ((cp >> 6) & 0x3F)), char(0x80 | (cp & 0x3F)) };
    return out;
}

// An installed font at a pixel size (Java's Font(name, PLAIN, size)), with
// Java's metrics: ascent and descent rounded up past .05, widths to nearest.
class Font {
public:
    Font(const std::string& family, int pixels) {
        std::string path = jResolveFontFace(family, false, false);
        if (path.empty()) {
            JLOGC(JPlacerLog::kPipeline, JLogLevel::Warn) << "font \"" << family << "\" not installed; using " << kFallbackFamily;
            path = jResolveFontFace(kFallbackFamily, false, false);
        }
        if (path.empty()) {
            const std::vector<JSystemFont> all = jListSystemFonts();
            if (all.empty()) throw std::runtime_error("No font is installed.");
            path = all.front().path;
        }
        m_data = fileData(path);
        if (!stbtt_InitFont(&m_info, m_data->data(), stbtt_GetFontOffsetForIndex(m_data->data(), 0)))
            throw std::runtime_error("Unable to read font " + path);
        m_scale = stbtt_ScaleForMappingEmToPixels(&m_info, float(pixels));
        int a, d, gap;
        stbtt_GetFontVMetrics(&m_info, &a, &d, &gap);
        m_ascent = int(0.95f + a * m_scale);
        m_descent = int(0.95f + -d * m_scale);
    }
    int ascent() const { return m_ascent; }
    int descent() const { return m_descent; }
    int width(char32_t cp) const {
        int advance, bearing;
        stbtt_GetCodepointHMetrics(&m_info, int(cp), &advance, &bearing);
        return int(0.5f + advance * m_scale);
    }
    // The character's coverage (0..1) added onto `mat` (8-bit, 1 or 3
    // channels) in `bgr` at `alpha`, its pen at x, its baseline at y.
    void draw(cv::Mat& mat, char32_t cp, double x, double y, const cv::Scalar& bgr, double alpha) const {
        const int px = int(std::floor(x)), py = int(std::floor(y));
        const float sx = float(x - px), sy = float(y - py);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(&m_info, int(cp), m_scale, m_scale, sx, sy, &x0, &y0, &x1, &y1);
        const int w = x1 - x0, h = y1 - y0;
        if (w <= 0 || h <= 0) return;
        std::vector<unsigned char> cov(size_t(w) * size_t(h));
        stbtt_MakeCodepointBitmapSubpixel(&m_info, cov.data(), w, h, w, m_scale, m_scale, sx, sy, int(cp));
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c) {
                const int X = px + x0 + c, Y = py + y0 + r;
                if (X < 0 || Y < 0 || X >= mat.cols || Y >= mat.rows) continue;
                const double a = alpha * cov[size_t(r) * size_t(w) + size_t(c)] / 255.0;
                if (mat.channels() == 1) {
                    uchar& v = mat.at<uchar>(Y, X);
                    v = cv::saturate_cast<uchar>(v * (1 - a) + bgr[0] * a);
                } else {
                    cv::Vec3b& v = mat.at<cv::Vec3b>(Y, X);
                    for (int k = 0; k < 3; ++k) v[k] = cv::saturate_cast<uchar>(v[k] * (1 - a) + bgr[k] * a);
                }
            }
    }

private:
    // A font file read once (pipelines may run on several threads).
    static std::shared_ptr<std::vector<unsigned char>> fileData(const std::string& path) {
        static std::mutex lock;
        static std::map<std::string, std::shared_ptr<std::vector<unsigned char>>> files;
        std::lock_guard<std::mutex> hold(lock);
        auto& d = files[path];
        if (!d) {
            std::ifstream f(path, std::ios::binary);
            d = std::make_shared<std::vector<unsigned char>>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            if (d->empty()) throw std::runtime_error("Unable to read font " + path);
        }
        return d;
    }

    std::shared_ptr<std::vector<unsigned char>> m_data;
    stbtt_fontinfo                              m_info {};
    float                                       m_scale = 1;
    int                                         m_ascent = 0, m_descent = 0;
};

// OpenPnP's CharacterMatch: a character found, and how its neighbours rate it.
struct CharacterMatch {
    char32_t ch;
    double   x, y, width, height, score;
    int      bonus = 0, malus = 0, charNum = 0;
    bool     excluded = false;

    // Scaled to the width, so narrow characters count as less good matches.
    double overallScore() const { return score + 0.05 * bonus - 0.05 * malus; }
    double tolerance() const { return height / 10.0 + 1; }
    bool   overlaps(const CharacterMatch& o) const {
        const double t = tolerance();
        return o.x + t < x + width && o.x + o.width - t > x && o.y + t < y + height && o.y + o.height - t > y;
    }
    bool expandsRight(const CharacterMatch& o) const {
        const double t = tolerance();
        return std::abs(o.x - x) < t && std::abs(o.y - y) < t && o.width < width;
    }
    bool expandsLeft(const CharacterMatch& o) const {
        const double t = tolerance();
        return std::abs(o.x + o.width - x - width) < t && std::abs(o.y - y) < t && o.width < width;
    }
    bool precedesInLine(const CharacterMatch& o) const {
        const double t = tolerance();
        return std::abs(o.x - (x + width)) < t && std::abs(o.y - y) < t;
    }
    std::string describe() const {
        std::ostringstream s;
        s << "CharacterMatch [x=" << x << ", y=" << y << ", width=" << width << ", height=" << height << ", ch=\"" << utf8(ch)
          << "\", score=" << score << ", charNum=" << charNum << "]";
        return s.str();
    }
};

// The picture as a debug file (a match map, 0..1, as grey).
void writeDebug(const JPPipeline& p, const std::string& name, const cv::Mat& mat) {
    const std::string& dir = p.context().debugDirectory;
    if (dir.empty()) return;
    cv::Mat out = mat;
    if (mat.depth() != CV_8U) mat.convertTo(out, CV_8U, 255);
    JPStageUtil::writePicture(dir + "/" + name + ".png", out);
}

Output performOcr(JPPipeline& p, JPPipelineStage& s, const std::string& fontName, double fontSizePt, const std::string& alphabet) {
    const bool debug = s.flag("debug");
    const double threshold = s.number("threshold");
    const int fontMaxPixelSize = s.integer("font-max-pixel-size");
    // Pixels a typographic point.
    double scalePt = 25.4 / 72.0 * p.context().pixelsPerMmY;
    const cv::Mat& working = p.workingImage();
    cv::Mat textImage = working;
    // Smaller first when the font is large in pixels (at least by half, or the picture suffers).
    double rescale = 1.0;
    if (fontMaxPixelSize >= 7 && fontMaxPixelSize < 0.5 * rescale * scalePt * fontSizePt) {
        rescale = fontMaxPixelSize / (rescale * scalePt * fontSizePt);
        if (debug) JLOGC(JPlacerLog::kPipeline, JLogLevel::Debug) << "[org.openpnp.vision.pipeline.stages.SimpleOcr] rescale of input = " << rescale;
    }
    if (rescale != 1.0) {
        cv::resize(textImage, textImage, cv::Size(int(textImage.cols * rescale), int(textImage.rows * rescale)));
        scalePt *= rescale;
    }
    if (textImage.type() == CV_32F) textImage.convertTo(textImage, CV_8UC1, 255);
    else if (textImage.type() != CV_8UC1 && textImage.type() != CV_8UC3) {
        std::ostringstream m;
        m << "Unsupported Mat: type " << textImage.type() << ", channels " << textImage.channels() << ", depth " << textImage.depth();
        throw std::runtime_error(m.str());
    }
    const Font font(fontName, int(JPStageUtil::javaRound(scalePt * fontSizePt)));
    const int maxAscent = font.ascent();
    const int height = maxAscent + font.descent();
    Output out;
    if (height < 5 || height >= textImage.rows) {
        out.image = textImage;
        out.model.value = Model::Ocr {};
        return out;
    }
    // Each character of the alphabet looked for (spaces are told by the gaps).
    std::vector<CharacterMatch> matches;
    for (const char32_t ch : codepoints(alphabet)) {
        if (ch == U' ') continue;
        const int width = font.width(ch);
        if (width <= 0) continue;
        cv::Mat templ(height, width, CV_8UC1, cv::Scalar(255));
        font.draw(templ, ch, 0, maxAscent, cv::Scalar::all(0), 1.0);
        if (textImage.channels() == 3) cv::cvtColor(templ, templ, cv::COLOR_GRAY2BGR);
        const std::string tag = (ch < 0x80 && std::isalnum(int(ch)) ? utf8(ch) : std::to_string(uint32_t(ch))) + "-";
        if (debug) writeDebug(p, "character-" + tag, templ);
        if (templ.cols > textImage.cols || templ.rows > textImage.rows) continue;
        cv::Mat map;
        cv::matchTemplate(textImage, templ, map, cv::TM_CCOEFF_NORMED);
        double maxVal = 0;
        cv::minMaxLoc(map, nullptr, &maxVal);
        for (const cv::Point& pt : JPStageUtil::matMaxima(map, threshold, maxVal))
            matches.push_back({ ch, double(pt.x), double(pt.y), double(templ.cols), double(templ.rows), double(map.at<float>(pt.y, pt.x)) });
        if (debug) writeDebug(p, "match-map-" + tag, map);
    }
    std::string text;
    double overallScore = 0;
    int numChars = 0;
    if (!matches.empty()) {
        // Overlaps count against, characters in a row for; at a word's ends
        // the widest wins (else an "r" in an "n" would).
        for (CharacterMatch& m : matches) {
            bool first = true, last = true;
            for (CharacterMatch& o : matches) {
                if (&o == &m) continue;
                if (m.overlaps(o)) {
                    o.malus++;
                    if (o.expandsLeft(m)) first = false;
                    if (o.expandsRight(m)) last = false;
                } else if (m.precedesInLine(o)) {
                    m.bonus++;
                    last = false;
                } else if (o.precedesInLine(m)) {
                    m.bonus++;
                    first = false;
                }
            }
            if (first) m.bonus++;
            if (last) m.bonus++;
        }
        // Of two overlapping, the better scored stays.
        for (const CharacterMatch& m : matches)
            for (CharacterMatch& o : matches)
                if (&m != &o && m.overlaps(o) && m.overallScore() > o.overallScore()) o.excluded = true;
        std::stable_sort(matches.begin(), matches.end(), [](const CharacterMatch& a, const CharacterMatch& b) { return a.x < b.x; });
        // The lines, top first.
        double prevLineY = -1;
        for (;;) {
            int left = 0;
            double nextLineY = std::numeric_limits<double>::max();
            for (CharacterMatch& m : matches) {
                if (m.excluded) continue;
                if (m.y < prevLineY) {
                    m.excluded = true;   // off the line grid
                } else {
                    ++left;
                    nextLineY = std::min(nextLineY, m.y);
                }
            }
            if (left == 0) break;
            if (!text.empty()) text += '\n';
            const double lineTolerance = double(height / 4);
            const CharacterMatch* prev = nullptr;
            for (CharacterMatch& m : matches) {
                if (m.excluded || std::abs(m.y - nextLineY) >= lineTolerance) continue;
                // A gap: one space, however wide.
                if (prev && !prev->precedesInLine(m)) text += ' ';
                text += utf8(m.ch);
                m.charNum = ++numChars;
                m.excluded = true;
                overallScore += m.overallScore();
                prev = &m;
                prevLineY = std::max(prevLineY, m.y + m.height - lineTolerance);
            }
        }
    }
    if (debug) {
        // Taken ones first, in order, then the rest by X.
        std::vector<CharacterMatch> shown = matches;
        std::stable_sort(shown.begin(), shown.end(), [](const CharacterMatch& a, const CharacterMatch& b) {
            if ((a.charNum > 0) != (b.charNum > 0)) return a.charNum > 0;
            if (a.charNum != b.charNum) return a.charNum < b.charNum;
            return a.x < b.x;
        });
        std::string all = "[";
        for (size_t i = 0; i < shown.size(); ++i) all += (i ? ", " : "") + shown[i].describe();
        JLOGC(JPlacerLog::kPipeline, JLogLevel::Debug) << "[org.openpnp.vision.pipeline.stages.SimpleOcr] matches = " << all << "]";
    }
    const std::string style = s.text("draw-style");
    if (style != "None") {
        double matchScale = 1.0;
        if (style == "OverOriginalImage" && rescale != 1.0) {
            textImage = working;
            if (textImage.type() == CV_32F) textImage.convertTo(textImage, CV_8UC1, 255);
            scalePt /= rescale;
            matchScale = 1.0 / rescale;
        }
        cv::Mat color;
        if (textImage.channels() == 1) cv::cvtColor(textImage, color, cv::COLOR_GRAY2BGR);
        else color = textImage.clone();
        const Font drawFont(fontName, int(JPStageUtil::javaRound(scalePt * fontSizePt)));
        for (const CharacterMatch& m : matches) {
            if (m.charNum <= 0) continue;
            // Green to red by the score.
            const double score = std::min(1.0, std::max(0.0, (m.score - threshold) / (1.0 - threshold)));
            drawFont.draw(color, m.ch, double(JPStageUtil::javaRound(m.x * matchScale)),
                          double(JPStageUtil::javaRound(m.y * matchScale + drawFont.ascent())), cv::Scalar(0, 255 * score, 255 * (1 - score)), 0.4);
        }
        textImage = color;
    }
    out.image = textImage;
    out.model.value = Model::Ocr { text, numChars, overallScore };
    return out;
}

Output decodeBarcode(JPPipeline& p) {
    const cv::Mat& working = p.workingImage();
    cv::Mat gray;
    if (working.channels() == 3) cv::cvtColor(working, gray, cv::COLOR_BGR2GRAY);
    else if (working.depth() != CV_8U) working.convertTo(gray, CV_8U, 255);
    else gray = working;
    // Shown as the reader saw it: black and white by the light around each pixel.
    Output out;
    cv::adaptiveThreshold(gray, out.image, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, kBinarizeBlock, 0);
    std::string text = cv::QRCodeDetector().detectAndDecode(gray);
    if (text.empty()) {
        std::vector<std::string> infos, types;
        std::vector<cv::Point2f> corners;
        if (cv::barcode::BarcodeDetector().detectAndDecodeWithType(gray, infos, types, corners))
            for (const std::string& i : infos)
                if (!i.empty()) {
                    text = i;
                    break;
                }
    }
    out.model.value = Model::Ocr { text, int(text.size()), double(text.size()) };
    return out;
}

} // namespace

void JPStageRegistry::addOcrStages(std::vector<JPStageType>& types) {
    types.push_back({ std::string(kStages) + "SimpleOcr", "",
                      "A very simple OCR/Barcode stage that returns a (multi-line) text string. <br/>Use an AffineWarp stage to extract the region of interest first (for cropping, rotation and acceptable speed).<br/>It is also recommended to convert the image to grayscale first. Do not apply a threshold stage.",
                      { P { "alphabet", Kind::Text, "0123456789.-+_RCLDQYXJIVAFH%GMKkmuµnp",
                            "Alphabet of all the characters that can be recognized. The smaller the alphabet, the faster and the more reliable the OCR works. The alphabet can be overriden with the \"alphabet\" property." },
                        P { "font-name", Kind::Text, "Liberation Mono",
                            "Name of the font to be recognized or [Barcode].<br/>Monospace fonts work much better and allow lower resolution. Use a font where all the used characters are easily distinguishable. Fonts with clear separation between characters are preferred." },
                        P { "font-size-pt", Kind::Number, "7.0", "Size of the font in typographic points (1 pt = 1/72 in)." },
                        P { "font-max-pixel-size", Kind::Integer, "20",
                            "If the font size is larger in pixels than this value, the OCR stage will resizes the image to a smaller resolution first (to achieve acceptable OCR speed). Alternatively, you can use an AffineWarp stage with scale < 1.0, but then you need to scale your pointSize too." },
                        P { "auto-detect-size", Kind::Flag, "false",
                            "<strong style=\"color:red;\">CAUTION, Quick&Dirty Hack:</strong> This will auto-detect the font size upwards and downwards of your currently set pointSize. This is a one-shot option, used while editing the pipeline. Once the detection is done, the switch is cleared.<br/>The process may take a while and appear to hang, be patient. Afterwards you need to switch stages to refresh the GUI." },
                        P { "threshold", Kind::Number, "0.75", "Template matching minimum match threshold (CCOEFF_NORMED method). Default is 0.75." },
                        P { "draw-style", Kind::Choice, "OverOriginalImage", "Draw the OCR match onto the image. ",
                            { "None", "OverScaledImage", "OverOriginalImage" } },
                        P { "debug", Kind::Flag, "false", "Write debug images and messages. Will slow down operation." },
                        P { "property-name", Kind::Text, "SimpleOcr",
                            "Property name as controlled by the vision operation using this pipeline.<br/>If set, these will override the properties configured here." } },
                      [](JPPipeline& p, JPPipelineStage& s) {
                          if (p.context().pixelsPerMmX <= 0 || p.context().pixelsPerMmY <= 0)
                              throw std::runtime_error("Property \"camera\" is required.");
                          const std::string control = s.text("property-name");
                          // An empty alphabet is also how a caller turns the stage off.
                          const std::string alphabet = p.overriddenText(s, "alphabet", s.text("alphabet"), control + ".alphabet");
                          if (alphabet.empty()) return Output {};
                          const std::string fontName = p.overriddenText(s, "font-name", s.text("font-name"), control + ".fontName");
                          if (fontName.empty()) return Output {};
                          if (fontName == kBarcodeFont) return decodeBarcode(p);
                          double fontSizePt = p.overridden(s, "font-size-pt", s.number("font-size-pt"), control + ".fontSizePt");
                          if (fontSizePt == 0.0) return Output {};
                          // While editing, once: the size read best, from half to twice the one set, 5 % apart.
                          if (s.flag("auto-detect-size")) {
                              s.set("auto-detect-size", "false");
                              const double set = s.number("font-size-pt");
                              double bestScore = -1, bestSize = NAN;
                              for (double size = set * 0.5; size < set * 2.0; size *= 1.05) {
                                  JLOGC(JPlacerLog::kPipeline, JLogLevel::Debug) << "[org.openpnp.vision.pipeline.stages.SimpleOcr] auto-detecting at font size = " << size << "pt";
                                  const Output o = performOcr(p, s, fontName, size, alphabet);
                                  const auto& r = std::get<Model::Ocr>(o.model.value);
                                  if (r.overallScore > 0.0 && (std::isnan(bestSize) || bestScore < r.overallScore)) {
                                      bestScore = r.overallScore;
                                      bestSize = size;
                                      JLOGC(JPlacerLog::kPipeline, JLogLevel::Debug) << "[org.openpnp.vision.pipeline.stages.SimpleOcr] new best font size = " << size
                                                                                     << "pt, overallScore = " << r.overallScore << ", text = " << r.text;
                                  }
                              }
                              if (!std::isnan(bestSize)) {
                                  std::ostringstream v;
                                  v << double(JPStageUtil::javaRound(bestSize * 100.0)) / 100.0;
                                  s.set("font-size-pt", v.str());
                                  fontSizePt = bestSize;
                              }
                          }
                          return performOcr(p, s, fontName, fontSizePt, alphabet);
                      } });
}

} // inline namespace jf
