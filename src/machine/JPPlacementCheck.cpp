// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPlacementCheck.h"

#include <algorithm>
#include <array>
#include <cmath>

inline namespace jf {

namespace {

// Solve the n x n system a x = y (row-major), partial pivoting; false when it is (near) singular.
bool solve(std::vector<double> a, std::vector<double> y, size_t n, std::vector<double>& x) {
    for (size_t c = 0; c < n; ++c) {
        size_t best = c;
        for (size_t r = c + 1; r < n; ++r)
            if (std::abs(a[r * n + c]) > std::abs(a[best * n + c])) best = r;
        if (std::abs(a[best * n + c]) < 1e-9) return false;
        if (best != c) {
            for (size_t k = 0; k < n; ++k) std::swap(a[c * n + k], a[best * n + k]);
            std::swap(y[c], y[best]);
        }
        for (size_t r = c + 1; r < n; ++r) {
            const double f = a[r * n + c] / a[c * n + c];
            for (size_t k = c; k < n; ++k) a[r * n + k] -= f * a[c * n + k];
            y[r] -= f * y[c];
        }
    }
    x.assign(n, 0);
    for (size_t r = n; r-- > 0;) {
        double s = y[r];
        for (size_t k = r + 1; k < n; ++k) s -= a[r * n + k] * x[k];
        x[r] = s / a[r * n + r];
    }
    return true;
}

// Least squares for the unknowns (bias x, y; turning x, y; and, with `object`, the object's offset x, y): each
// placement two equations,
//   ex = bx + cos(a) tx - sin(a) ty + cos(t) ox - sin(t) oy,   ey = by + sin(a) tx + cos(a) ty + sin(t) ox + cos(t) oy,
// a its angle, t its turn in all.
bool fitted(const std::vector<JPPlacementCheck::Place>& places, bool object, std::vector<double>& x) {
    const size_t n = object ? 6 : 4;
    std::vector<double> ata(n * n, 0), aty(n, 0);
    auto add = [&](const std::array<double, 6>& row, double y) {
        for (size_t i = 0; i < n; ++i) {
            aty[i] += row[i] * y;
            for (size_t j = 0; j < n; ++j) ata[i * n + j] += row[i] * row[j];
        }
    };
    for (const JPPlacementCheck::Place& p : places) {
        const double a = p.angle * M_PI / 180, t = p.turned * M_PI / 180;
        add({ 1, 0, std::cos(a), -std::sin(a), std::cos(t), -std::sin(t) }, p.errorX);
        add({ 0, 1, std::sin(a), std::cos(a), std::sin(t), std::cos(t) }, p.errorY);
    }
    return places.size() * 2 >= n && solve(ata, aty, n, x);
}

} // namespace

JPPlacementCheck::Fit JPPlacementCheck::fit() const {
    Fit f;
    std::vector<double> x;
    if (fitted(places, true, x)) {
        f = { x[0], x[1], x[2], x[3], x[4], x[5], true };
    } else if (fitted(places, false, x)) {
        f = { x[0], x[1], x[2], x[3], 0, 0, false };
    } else if (!places.empty()) {
        for (const Place& p : places) {
            f.biasX += p.errorX / double(places.size());
            f.biasY += p.errorY / double(places.size());
        }
    }
    return f;
}

void JPPlacementCheck::turning(double& x, double& y) const {
    const Fit f = fit();
    x = f.turnX;
    y = f.turnY;
}

bool JPPlacementCheck::objectOffset(double& x, double& y) const {
    const Fit f = fit();
    x = f.objectX;
    y = f.objectY;
    return f.object;
}

double JPPlacementCheck::residual(const Place& p) const {
    const Fit f = fit();
    const double a = p.angle * M_PI / 180, t = p.turned * M_PI / 180;
    const double rx = p.errorX - f.biasX - (f.turnX * std::cos(a) - f.turnY * std::sin(a)) - (f.objectX * std::cos(t) - f.objectY * std::sin(t));
    const double ry = p.errorY - f.biasY - (f.turnX * std::sin(a) + f.turnY * std::cos(a)) - (f.objectX * std::sin(t) + f.objectY * std::cos(t));
    return std::hypot(rx, ry);
}

double JPPlacementCheck::worstResidual() const {
    double most = 0;
    for (const Place& p : places) most = std::max(most, residual(p));
    return most;
}

double JPPlacementCheck::driftX() const {
    double s = 0;
    for (const Drift& d : drifts) s += d.dx;
    return drifts.empty() ? 0 : s / double(drifts.size());
}

double JPPlacementCheck::driftY() const {
    double s = 0;
    for (const Drift& d : drifts) s += d.dy;
    return drifts.empty() ? 0 : s / double(drifts.size());
}

void JPPlacementCheck::visionBias(double& x, double& y) const {
    // Each offset bottom vision saw, less what the object's own offset put it off the nozzle by (picked where the head
    // camera found it: its true centre the object's offset away, turned as it then was, and turned again by the
    // angle): what is left turns with the angle and averages away but bottom vision's bias.
    x = y = 0;
    if (places.empty()) return;
    const Fit f = fit();
    for (const Place& p : places) {
        const double r = (p.angle + p.heldTurn) * M_PI / 180;
        x += p.offsetX + (f.objectX * std::cos(r) - f.objectY * std::sin(r));
        y += p.offsetY + (f.objectX * std::sin(r) + f.objectY * std::cos(r));
    }
    x /= double(places.size());
    y /= double(places.size());
}

JPPlacementCheck JPPlacementCheck::fromJson(const JJson& j) {
    JPPlacementCheck c;
    c.when = j["when"].str();
    c.z = j["z"].number();
    for (const JJson& d : j["drifts"].arr()) c.drifts.push_back({ d["x"].number(), d["y"].number() });
    for (const JJson& p : j["places"].arr())
        c.places.push_back({ p["angle"].number(), p["offset"]["x"].number(), p["offset"]["y"].number(), p["error"]["x"].number(),
                             p["error"]["y"].number(), p["heldTurn"].number(), p["turned"].number() });
    return c;
}

JJson JPPlacementCheck::toJson() const {
    JJson j = JJson::object();
    j["when"] = when;
    j["z"] = z;
    j["drifts"] = JJson::array();
    for (const Drift& d : drifts) {
        JJson o = JJson::object();
        o["x"] = d.dx;
        o["y"] = d.dy;
        j["drifts"].push(o);
    }
    j["places"] = JJson::array();
    for (const Place& p : places) {
        JJson o = JJson::object();
        o["angle"] = p.angle;
        o["offset"]["x"] = p.offsetX;
        o["offset"]["y"] = p.offsetY;
        o["error"]["x"] = p.errorX;
        o["error"]["y"] = p.errorY;
        o["heldTurn"] = p.heldTurn;
        o["turned"] = p.turned;
        j["places"].push(o);
    }
    return j;
}

} // inline namespace jf
