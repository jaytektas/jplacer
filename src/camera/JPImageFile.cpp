// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImageFile.h"

#include <png.h>


inline namespace jf {

bool JPImageFile::writePng(const std::string& path, const JPFrame& frame, std::string& error) {
    if (frame.width <= 0 || frame.height <= 0 || frame.rgba.size() < size_t(frame.width) * size_t(frame.height) * 4) {
        error = path + ": no picture to save";
        return false;
    }
    png_image img{};
    img.version = PNG_IMAGE_VERSION;
    img.width   = png_uint_32(frame.width);
    img.height  = png_uint_32(frame.height);
    img.format  = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&img, path.c_str(), 0, frame.rgba.data(), 0, nullptr)) {
        error = path + ": " + img.message;
        return false;
    }
    return true;
}

bool JPImageFile::readPng(const std::string& path, JPFrame& frame, std::string& error) {
    png_image img{};
    img.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&img, path.c_str())) {
        error = path + ": " + img.message;
        return false;
    }
    img.format = PNG_FORMAT_RGBA;
    frame.width  = int(img.width);
    frame.height = int(img.height);
    frame.rgba.resize(PNG_IMAGE_SIZE(img));
    if (!png_image_finish_read(&img, nullptr, frame.rgba.data(), 0, nullptr)) {
        error = path + ": " + img.message;
        png_image_free(&img);
        return false;
    }
    return true;
}

} // inline namespace jf
