// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStraightPicture.h"

#include "camera/JPStraightener.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

inline namespace jf {

namespace {

// Straightened pictures kept made, for the calibrations last asked for (a camera or two, at a height or two).
constexpr size_t kKept = 6;

} // namespace

std::shared_ptr<const JPStraightPicture> JPStraightPicture::of(const JPCameraCalibration& cal, bool lookingUp, double showAll) {
    static std::mutex                                                              lock;
    static std::vector<std::pair<std::string, std::shared_ptr<const JPStraightPicture>>> kept;
    const std::string key = cal.toJson().dump() + "|" + std::to_string(cal.viewShiftX) + "," + std::to_string(cal.viewShiftY) + "|"
                            + (lookingUp ? "up" : "down") + "|" + std::to_string(showAll);
    {
        const std::lock_guard<std::mutex> held(lock);
        for (auto i = kept.begin(); i != kept.end(); ++i)
            if (i->first == key) {
                auto found = i->second;
                kept.erase(i);
                kept.emplace_back(key, found);   // the newest last
                return found;
            }
    }
    const std::optional<JPStraightener> s = JPStraightener::make(cal, lookingUp, showAll);
    if (!s) return nullptr;
    auto made = std::make_shared<JPStraightPicture>();
    // A perfect lens, mounted square, at the straightened scale: the camera's move (mm) to straightened pixels is
    // the straightener's, and the middle of the picture where it looks, as the camera's.
    JPCameraCalibration& c = made->m_cal;
    c = cal;
    c.lensK1 = c.lensK2 = 0;
    c.lensCentreX = cal.width / 2.0;
    c.lensCentreY = cal.height / 2.0;
    c.pxPerMm = { s->scaleX(), 0, 0, s->scaleY() };
    c.secondScale *= c.scale() / cal.scale();   // the same camera at its second height, straightened alike
    c.points.clear();                           // measured on the picture as taken
    // Each straightened pixel from where it is in the picture as taken.
    made->m_mapX.create(cal.height, cal.width, CV_32F);
    made->m_mapY.create(cal.height, cal.width, CV_32F);
    cv::parallel_for_(cv::Range(0, cal.height), [&](const cv::Range& rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            float* mx = made->m_mapX.ptr<float>(y);
            float* my = made->m_mapY.ptr<float>(y);
            for (int x = 0; x < cal.width; ++x) {
                double rx, ry;
                const bool seen = s->toRaw(x, y, rx, ry);
                mx[x] = seen ? float(rx) : -1.f;
                my[x] = seen ? float(ry) : -1.f;
            }
        }
    });
    const std::lock_guard<std::mutex> held(lock);
    if (kept.size() >= kKept) kept.erase(kept.begin());
    kept.emplace_back(key, made);
    return made;
}

bool JPStraightPicture::straighten(const cv::Mat& raw, cv::Mat& out) const {
    if (raw.cols != m_mapX.cols || raw.rows != m_mapX.rows) return false;
    cv::remap(raw, out, m_mapX, m_mapY, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar::all(0));
    return true;
}

} // inline namespace jf
