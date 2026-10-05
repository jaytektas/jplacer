// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's Vision Compositing, as jplacer works it out: for OpenPnP's own
// compositing test packages, the same solution and the same shots (middle,
// size, mask radii, configuration, optional or not, corners) as OpenPnP's
// VisionCompositing.Composite finds (tests/data/openpnp/compositing).
// Then a part seen in its shots, its corners put together: its centre,
// angle and size found again.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "model/JPPackage.h"
#include "openpnp/JPXmlReader.h"
#include "tasks/JPVisionComposite.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace jf;

namespace {

JPVisionComposite::Input inputFor(const JPPackage& pkg) {
    JPVisionComposite::Input in;
    in.packageId = pkg.id;
    in.footprint = pkg.footprint;
    in.compositing = pkg.visionCompositing.value_or(JPVisionCompositing {});
    in.toleranceMm = 1;
    in.maxPartDiameterMm = 20;
    in.cameraWidthMm = 640 * 0.0134375;
    in.cameraHeightMm = 480 * 0.0134375;
    in.roamingRadiusMm = 30;
    in.cameraName = "SimulatedUpCamera";
    return in;
}

bool near(double a, double b) { return std::abs(a - b) < 0.0011; }

} // namespace

int main() {
    const std::string dir = std::string(JPLACER_TESTDATA_DIR) + "/openpnp/compositing/";
    JPXmlElement root;
    std::string error;
    assert(JPXmlReader::read(dir + "packages.xml", root, error));
    std::map<std::string, JPPackage> packages;
    for (const JPXmlElement& e : root.children)
        if (e.name == "package") {
            JPPackage p = JPPackage::fromXml(e);
            packages.emplace(p.id, p);
        }
    // Each line: PKG id Solution shots=n [x,y wxh rmin-max Config( opt) cN] ...
    std::ifstream expected(dir + "expected.txt");
    std::string line;
    int checked = 0;
    while (std::getline(expected, line)) {
        std::istringstream in(line);
        std::string tag, id, solution, shotsWord;
        in >> tag >> id >> solution >> shotsWord;
        const JPPackage& pkg = packages.at(id);
        const JPVisionComposite composite(inputFor(pkg));
        if (solution != JPVisionComposite::solutionName(composite.solution()))
            std::fprintf(stderr, "%s: %s, OpenPnP %s\n", id.c_str(), JPVisionComposite::solutionName(composite.solution()), solution.c_str());
        assert(solution == JPVisionComposite::solutionName(composite.solution()));
        const size_t n = std::stoul(shotsWord.substr(6));
        assert(composite.shots().size() == n);
        for (size_t i = 0; i < n; ++i) {
            std::string shot;
            std::getline(in, shot, '[');
            std::getline(in, shot, ']');
            double x, y, w, h, r0, r1;
            char config[32] = {};
            int corners = 0;
            const bool optional = shot.find(" opt") != std::string::npos;
            assert(std::sscanf(shot.c_str(), "%lf,%lf %lfx%lf r%lf-%lf %31s", &x, &y, &w, &h, &r0, &r1, config) == 7);
            assert(std::sscanf(shot.substr(shot.rfind(" c") + 2).c_str(), "%d", &corners) == 1);
            const JPVisionComposite::Shot& s = composite.shots()[i];
            const bool same = near(s.x, x) && near(s.y, y) && near(s.width, w) && near(s.height, h) && near(s.minMaskRadius, r0)
                           && near(s.maxMaskRadius, r1) && std::string(JPVisionComposite::configurationName(s.configuration)) == config
                           && s.optional == optional && int(s.corners.size()) == corners;
            if (!same)
                std::fprintf(stderr, "%s shot %zu: [%.3f,%.3f %.3fx%.3f r%.3f-%.3f %s%s c%zu], OpenPnP [%s]\n", id.c_str(), i, s.x, s.y,
                             s.width, s.height, s.minMaskRadius, s.maxMaskRadius,
                             JPVisionComposite::configurationName(s.configuration), s.optional ? " opt" : "", s.corners.size(),
                             shot.c_str());
            assert(same);
        }
        ++checked;
    }
    assert(checked == 12);

    // A Box solution's shots seeing the part 0.25 mm right, 0.75 mm up and
    // turned 2°: its corners where they would be, put together.
    {
        JPVisionComposite composite(inputFor(packages.at("LQFP144")));
        assert(JPVisionComposite::isAdvanced(composite.solution()));
        const double angle = 2 * M_PI / 180, dx = 0.25, dy = 0.75;
        auto seen = [&](double x, double y) {
            return JPVisionComposite::Point { dx + x * std::cos(angle) - y * std::sin(angle), dy + x * std::sin(angle) + y * std::cos(angle) };
        };
        for (const JPVisionComposite::Shot* shot : composite.travel(0, 0)) {
            // Each corner where it is, the other three of its rectangle anywhere (they are not used).
            std::array<JPVisionComposite::Point, 4> points {};
            for (const JPVisionComposite::Corner* c : shot->corners)
                points[size_t((c->xSign < 0 ? 0 : 1) + (c->ySign > 0 ? 0 : 2))] = seen(c->x, c->y);
            composite.accumulate(*shot, points);
        }
        JPVisionComposite::Detected d;
        std::string why;
        assert(composite.interpret(0, d, why));
        assert(std::abs(d.center.x - dx) < 1e-9 && std::abs(d.center.y - dy) < 1e-9);
        assert(std::abs(d.angle - 2) < 1e-9);
        assert(std::abs(d.scale.x - 1) < 1e-9 && std::abs(d.scale.y - 1) < 1e-9);
    }
    // No roaming radius: compositing forbidden, one shot.
    {
        JPVisionComposite::Input in = inputFor(packages.at("LQFP144"));
        in.roamingRadiusMm = 0;
        const JPVisionComposite composite(in);
        assert(composite.solution() == JPVisionComposite::Solution::NoCameraRoaming && composite.shots().size() == 1);
    }
    return 0;
}
