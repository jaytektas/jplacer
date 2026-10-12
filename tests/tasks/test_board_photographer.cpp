// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// A board's photo taken (JPBoardPhotographer) on a machine whose camera sees a known bed: the board's area covered
// by pictures, each row the other way from the last, those the camera cannot reach left out and said; the photo kept
// beside the board's file with what it is (JPBoardPhoto), found again, shown as the bed is where the board lies;
// deleted, gone. A board with no file, or no size and no placements, is not photographed (why said).
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "FakeJobMachine.h"

#include "camera/JPImageFile.h"
#include "model/JPBoard.h"
#include "model/JPBoardLocation.h"
#include "tasks/JPBoardPhotographer.h"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <filesystem>
#include <random>

using namespace jf;

namespace {

constexpr double kRes = 0.05;
constexpr int    kBedW = 2400, kBedH = 1600;   // 120 x 80 mm, machine (0, 80) at its top-left
constexpr int    kCamW = 400, kCamH = 300;     // 20 x 15 mm

cv::Mat bed() {
    std::mt19937 rng(3);
    cv::Mat noise(kBedH, kBedW, CV_8UC3);
    cv::randu(noise, 0, 255);
    cv::Mat b;
    cv::GaussianBlur(noise, b, { 0, 0 }, 3);
    std::uniform_int_distribution<int> x(0, kBedW), y(0, kBedH), r(4, 30);
    for (int i = 0; i < 700; ++i) cv::circle(b, { x(rng), y(rng) }, r(rng), cv::Scalar(255, 255 * (i % 2), 0), cv::FILLED);
    return b;
}

// A camera over the bed: it sees what is there, and reaches no further right than `reachX`.
class BedMachine : public FakeJobMachine {
public:
    explicit BedMachine(cv::Mat b) : m_bed(std::move(b)) {}
    double reachX = 1e9;
    std::vector<JPLocation> looked;

    bool cameraReaches(const JPLocation& at) const override { return at.x() <= reachX; }
    bool cameraSight(Sight& sight, std::string&) override {
        sight = {};
        sight.mmPerPixelX = sight.mmPerPixelY = kRes;
        sight.width = kCamW;
        sight.height = kCamH;
        return true;
    }
    bool snapshot(const JPLocation& at, double, Snapshot& shot, std::string&) override {
        looked.push_back(at);
        const double x0 = at.x() - kCamW / 2 * kRes, y0 = at.y() + kCamH / 2 * kRes;
        cv::Mat m = (cv::Mat_<double>(2, 3) << 1, 0, x0 / kRes, 0, 1, (80 - y0) / kRes);
        cv::warpAffine(m_bed, shot.bgr, m, { kCamW, kCamH }, cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
        shot.toMachine = [x0, y0](double px, double py, double& x, double& y) {
            x = x0 + px * kRes;
            y = y0 - py * kRes;
            return true;
        };
        return true;
    }

private:
    cv::Mat m_bed;
};

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-board-photographer";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const cv::Mat b = bed();
    BedMachine machine(b);

    auto board = std::make_shared<JPBoard>();
    board->name = "Test Board";
    board->file = (dir / "test.jpboard").string();
    board->setDimensions(JPLocation(JPLengthUnit::Millimeters, 40, 30, 0, 0));
    JPBoardLocation location;
    location.id = "Brd1";
    location.holder = board;
    location.setLocation(JPLocation(JPLengthUnit::Millimeters, 30, 20, 1.6, 10));

    // Planned: rows from the top, each the other way from the last.
    JPBoardPhotographer p(machine, {});
    std::string why;
    assert(p.begin(location, JPBoardPhoto::Kind::JobEnd, "a.jpjob", "run-1", why));
    assert(p.count() > 4 && p.taken() == 0 && !p.done() && p.boardName() == "Test Board");
    while (!p.done()) assert(p.takeNext(why));
    assert(int(machine.looked.size()) == p.count());
    assert(machine.looked[0].y() > machine.looked.back().y());   // from the top down
    assert(machine.looked[0].x() < machine.looked[1].x());       // the first row left to right
    JPBoardPhoto photo;
    assert(p.finish(photo, why));
    assert(fs::exists(photo.file) && fs::exists(JPBoardPhoto::recordOf(photo.file)));
    assert(photo.width == 840 && photo.height == 640 && photo.mmPerPixel == kRes && photo.notes.empty());
    assert(photo.run == "run-1" && photo.job == "a.jpjob" && photo.boardLocation == "Brd1" && photo.side == JPSide::Top);
    // Found again beside the board, as it was kept.
    const auto found = JPBoardPhoto::of(board->file);
    assert(found.size() == 1 && found[0].file == photo.file && found[0].kind == JPBoardPhoto::Kind::JobEnd);
    assert(found[0].originX == photo.originX && found[0].label().find("Job end") != std::string::npos);
    // The photo is the bed where the board lies.
    {
        JPFrame f;
        assert(JPImageFile::readPng(photo.file, f, why) && f.width == photo.width);
        const JPAffineTransform t = location.localToGlobalTransform();
        double diff = 0;
        int n = 0;
        for (int row = 20; row < f.height - 20; row += 37)
            for (int col = 20; col < f.width - 20; col += 41) {
                double mx = 0, my = 0;
                t.apply(photo.originX + col * kRes, photo.originY - row * kRes, mx, my);
                cv::Mat one;
                cv::getRectSubPix(b, { 1, 1 }, cv::Point2f(float(mx / kRes - 0.5), float((80 - my) / kRes - 0.5)), one);
                const cv::Vec3b q = one.at<cv::Vec3b>(0, 0);   // BGR
                const uint8_t* px = &f.rgba[(size_t(row) * f.width + col) * 4];
                diff += std::abs(px[0] - q[2]) + std::abs(px[1] - q[1]) + std::abs(px[2] - q[0]);
                n += 3;
                assert(px[3] == 255);
            }
        assert(diff / n < 4);
    }
    // Deleted: gone.
    assert(found[0].remove(why) && JPBoardPhoto::of(board->file).empty());

    // Out of the camera's reach on the right: those pictures not taken, and said.
    {
        BedMachine near(b);
        near.reachX = 45;
        JPBoardPhoto shot;
        assert(JPBoardPhotographer::photograph(near, {}, location, JPBoardPhoto::Kind::Asked, "", "", {}, shot, why));
        assert(!shot.notes.empty() && shot.notes.back().find("not in the photo") != std::string::npos);
        assert(std::any_of(shot.notes.begin(), shot.notes.end(),
                           [](const std::string& s) { return s.find("out of the head camera's reach") != std::string::npos; }));
    }
    // Stopped part way: none kept.
    {
        const size_t before = JPBoardPhoto::of(board->file).size();
        JPBoardPhoto shot;
        int told = 0;
        assert(!JPBoardPhotographer::photograph(machine, {}, location, JPBoardPhoto::Kind::Asked, "", "",
                                                [&told](int, int) { return ++told < 3; }, shot, why));
        assert(why == "Stopped" && JPBoardPhoto::of(board->file).size() == before);
    }
    // No size: its placements' spread; no placements either, or no file: not photographed.
    {
        auto bare = std::make_shared<JPBoard>();
        bare->file = (dir / "bare.jpboard").string();
        JPBoardLocation l;
        l.holder = bare;
        JPBoardPhotographer q(machine, {});
        assert(!q.begin(l, JPBoardPhoto::Kind::Asked, "", "", why) && why.find("no size") != std::string::npos);
        JPPlacement pl;
        pl.id = "R1";
        pl.location = JPLocation(JPLengthUnit::Millimeters, 10, 10, 0, 0);
        bare->placements.push_back(pl);
        pl.id = "R2";
        pl.location = JPLocation(JPLengthUnit::Millimeters, 20, 15, 0, 0);
        bare->placements.push_back(pl);
        assert(q.begin(l, JPBoardPhoto::Kind::Asked, "", "", why) && q.count() >= 1);
        bare->file.clear();
        assert(!q.begin(l, JPBoardPhoto::Kind::Asked, "", "", why) && why.find("save it") != std::string::npos);
    }
    fs::remove_all(dir);
    return 0;
}
