// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's ModelUnitsTest: lengths, areas and volumes converted through every unit and back, added, subtracted,
// multiplied and divided into one another, and parsed ("μ" or "u", "²" or "2"), equal to 48 bits as it checks.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "model/JPArea.h"
#include "model/JPLength.h"
#include "model/JPVolume.h"

using namespace jf;

namespace {

using L = JPLengthUnit;
using A = JPAreaUnit;
using V = JPVolumeUnit;

// OpenPnP's roundTo48Bits: the last 4 bits rounded off.
double roundTo48Bits(double d) {
    std::uint64_t x;
    std::memcpy(&x, &d, sizeof x);
    std::uint64_t r = x & 0xfffffffffffffff0ULL;
    if (x & 0x8ULL) r += 16;
    double out;
    std::memcpy(&out, &r, sizeof out);
    return out;
}

void eq48(double a, double b) { assert(roundTo48Bits(a) == roundTo48Bits(b)); }
template <typename Q> void eq48(const Q& a, const Q& b) {
    eq48(a.value(), b.value());
    assert(a.units() == b.units());
}

void testLengths() {
    const std::string xStr = "15.0";
    const double x = 15.0;
    const JPLength length1(x, L::Millimeters);
    eq48(x, length1.value());
    eq48(x / 10, length1.convertToUnits(L::Centimeters).value());
    eq48(x / 304.8, length1.convertToUnits(L::Feet).value());
    eq48(x / 25.4, length1.convertToUnits(L::Inches).value());
    eq48(x / 1000, length1.convertToUnits(L::Meters).value());
    eq48(x * 1000, length1.convertToUnits(L::Microns).value());
    eq48(x, length1.convertToUnits(L::Millimeters).value());
    eq48(x * 1000 / 25.4, length1.convertToUnits(L::Mils).value());

    eq48(length1, length1.convertToUnits(L::Centimeters).convertToUnits(L::Feet).convertToUnits(L::Meters)
                      .convertToUnits(L::Inches).convertToUnits(L::Microns).convertToUnits(L::Mils)
                      .convertToUnits(L::Millimeters));

    const std::string yStr = "0.3";
    const double y = 0.3;
    const JPLength length2(y, L::Centimeters);
    eq48(JPLength(x + 10 * y, L::Millimeters), length1.add(length2));
    eq48(JPLength(x + y, L::Millimeters), length1.add(y));
    eq48(JPLength(x - 10 * y, L::Millimeters), length1.subtract(length2));
    eq48(JPLength(x - y, L::Millimeters), length1.subtract(y));
    eq48(x / (10 * y), length1.divide(length2));
    eq48(JPLength(x / y, L::Millimeters), length1.divide(y));
    eq48(JPLength(std::fmod(x, 10 * y), L::Millimeters), length1.modulo(length2));
    eq48(JPArea(x * 10 * y, A::SquareMillimeters), length1.multiply(length2));
    eq48(JPLength(x * y, L::Millimeters), length1.multiply(y));

    eq48(length1, *JPLength::parse(xStr + "mm"));
    eq48(length2, *JPLength::parse(yStr + "cm"));
    eq48(JPLength(x, L::Microns), *JPLength::parse(xStr + "\xCE\xBCm"));
    eq48(*JPLength::parse(xStr + "\xCE\xBCm"), *JPLength::parse(xStr + "um"));
}

void testAreas() {
    const std::string xStr = "15.0";
    const double x = 15.0;
    const JPArea area1(x, A::SquareMillimeters);
    eq48(x, area1.value());
    eq48(x / std::pow(10, 2), area1.convertToUnits(A::SquareCentimeters).value());
    eq48(x / std::pow(304.8, 2), area1.convertToUnits(A::SquareFeet).value());
    eq48(x / std::pow(25.4, 2), area1.convertToUnits(A::SquareInches).value());
    eq48(x / std::pow(1000, 2), area1.convertToUnits(A::SquareMeters).value());
    eq48(x * std::pow(1000, 2), area1.convertToUnits(A::SquareMicrons).value());
    eq48(x, area1.convertToUnits(A::SquareMillimeters).value());
    eq48(x * std::pow(1000 / 25.4, 2), area1.convertToUnits(A::SquareMils).value());

    eq48(area1, area1.convertToUnits(A::SquareCentimeters).convertToUnits(A::SquareFeet).convertToUnits(A::SquareMeters)
                    .convertToUnits(A::SquareInches).convertToUnits(A::SquareMicrons).convertToUnits(A::SquareMils)
                    .convertToUnits(A::SquareMillimeters));

    const double y = 0.3;
    const JPArea area2(y, A::SquareCentimeters);
    eq48(JPArea(x + 100 * y, A::SquareMillimeters), area1.add(area2));
    eq48(JPArea(x + y, A::SquareMillimeters), area1.add(y));
    eq48(JPArea(x - 100 * y, A::SquareMillimeters), area1.subtract(area2));
    eq48(JPArea(x - y, A::SquareMillimeters), area1.subtract(y));
    eq48(JPArea(x * y, A::SquareMillimeters), area1.multiply(y));
    eq48(JPVolume(x * 10 * y, V::MicroLiters), area1.multiply(JPLength(y, L::Centimeters)));
    eq48(JPArea(x / y, A::SquareMillimeters), area1.divide(y));
    eq48(JPLength(x / (10 * y), L::Millimeters), area1.divide(JPLength(y, L::Centimeters)));
    eq48(x / (100 * y), area1.divide(area2));
    eq48(JPArea(std::fmod(x, 100 * y), A::SquareMillimeters), area1.modulo(area2));

    eq48(area1, *JPArea::parse(xStr + "mm\xC2\xB2"));
    eq48(JPArea(x, A::SquareMicrons), *JPArea::parse(xStr + "\xCE\xBCm2"));
    eq48(*JPArea::parse(xStr + "\xCE\xBCm2"), *JPArea::parse(xStr + "um2"));
}

void testVolumes() {
    const std::string xStr = "15.0";
    const double x = 15.0;
    const JPVolume volume1(x, V::MicroLiters);
    eq48(x, volume1.value());
    eq48(x / std::pow(10, 3), volume1.convertToUnits(V::CubicCentimeters).value());
    eq48(x / std::pow(304.8, 3), volume1.convertToUnits(V::CubicFeet).value());
    eq48(x / std::pow(25.4, 3), volume1.convertToUnits(V::CubicInches).value());
    eq48(x / std::pow(1000, 3), volume1.convertToUnits(V::CubicMeters).value());
    eq48(x * std::pow(1000, 3), volume1.convertToUnits(V::CubicMicrons).value());
    eq48(x, volume1.convertToUnits(V::CubicMillimeters).value());
    eq48(x * std::pow(1000 / 25.4, 3), volume1.convertToUnits(V::CubicMils).value());

    eq48(volume1, volume1.convertToUnits(V::CubicCentimeters).convertToUnits(V::CubicFeet).convertToUnits(V::CubicMeters)
                      .convertToUnits(V::CubicInches).convertToUnits(V::CubicMicrons).convertToUnits(V::CubicMils)
                      .convertToUnits(V::CubicMillimeters).convertToUnits(V::FemtoLiters).convertToUnits(V::MilliLiters)
                      .convertToUnits(V::MicroLiters));

    const double y = 0.3;
    const JPVolume volume2(y, V::MilliLiters);
    eq48(JPVolume(x + 1000 * y, V::MicroLiters), volume1.add(volume2));
    eq48(JPVolume(x + y, V::MicroLiters), volume1.add(y));
    eq48(JPVolume(x - 1000 * y, V::MicroLiters), volume1.subtract(volume2));
    eq48(JPVolume(x - y, V::MicroLiters), volume1.subtract(y));
    eq48(JPVolume(x * y, V::MicroLiters), volume1.multiply(y));
    eq48(JPVolume(x / y, V::MicroLiters), volume1.divide(y));
    eq48(JPArea(x / (10 * y), A::SquareMillimeters), volume1.divide(JPLength(y, L::Centimeters)));
    eq48(JPLength(x / (100 * y), L::Millimeters), volume1.divide(JPArea(y, A::SquareCentimeters)));
    eq48(x / (1000 * y), volume1.divide(volume2));
    eq48(JPVolume(std::fmod(x, 100 * y), V::MicroLiters), volume1.modulo(volume2));

    eq48(volume1, *JPVolume::parse(xStr + "ul"));
    eq48(JPVolume(x, V::MicroLiters), *JPVolume::parse(xStr + "\xCE\xBCl"));
    eq48(*JPVolume::parse(xStr + "\xCE\xBCl"), *JPVolume::parse(xStr + "ul"));
}

} // namespace

int main() {
    testLengths();
    testAreas();
    testVolumes();
    return 0;
}
