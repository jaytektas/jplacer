// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPPixels.h"

#include <algorithm>
#include <csetjmp>
#include <cstdio>

#include <jpeglib.h>

inline namespace jf {

namespace {

uint8_t clamp8(int v) { return uint8_t(std::clamp(v, 0, 255)); }

// libjpeg reports a fatal error by calling error_exit, which must not return:
// jump back out of the decode and say what it said.
struct JpegError {
    jpeg_error_mgr base;
    std::jmp_buf   jump;
    char           message[JMSG_LENGTH_MAX];
};

void onJpegError(j_common_ptr info) {
    auto* e = reinterpret_cast<JpegError*>(info->err);
    (*info->err->format_message)(info, e->message);
    std::longjmp(e->jump, 1);
}

} // namespace

void JPPixels::yuyvToRgba(const uint8_t* yuyv, int width, int height, std::vector<uint8_t>& rgba) {
    rgba.resize(size_t(width) * size_t(height) * 4);
    uint8_t* out = rgba.data();
    const size_t pairs = size_t(width) * size_t(height) / 2;
    for (size_t i = 0; i < pairs; ++i, yuyv += 4) {
        // BT.601, integer arithmetic.
        const int u = yuyv[1] - 128, v = yuyv[3] - 128;
        for (int k = 0; k < 2; ++k) {
            const int c = (yuyv[k * 2] - 16) * 298;
            *out++ = clamp8((c + 409 * v + 128) >> 8);
            *out++ = clamp8((c - 100 * u - 208 * v + 128) >> 8);
            *out++ = clamp8((c + 516 * u + 128) >> 8);
            *out++ = 255;
        }
    }
}

bool JPPixels::jpegToRgba(const uint8_t* data, size_t size, int& width, int& height,
                          std::vector<uint8_t>& rgba, std::string& error) {
    jpeg_decompress_struct info{};
    JpegError err{};
    info.err = jpeg_std_error(&err.base);
    err.base.error_exit = onJpegError;
    if (setjmp(err.jump)) {
        error = err.message;
        jpeg_destroy_decompress(&info);
        return false;
    }
    jpeg_create_decompress(&info);
    jpeg_mem_src(&info, data, static_cast<unsigned long>(size));
    jpeg_read_header(&info, TRUE);
    info.out_color_space = JCS_EXT_RGBA;   // libjpeg-turbo writes RGBA directly
    jpeg_start_decompress(&info);
    width  = int(info.output_width);
    height = int(info.output_height);
    rgba.resize(size_t(width) * size_t(height) * 4);
    while (info.output_scanline < info.output_height) {
        JSAMPROW row = rgba.data() + size_t(info.output_scanline) * size_t(width) * 4;
        jpeg_read_scanlines(&info, &row, 1);
    }
    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    return true;
}

} // inline namespace jf
