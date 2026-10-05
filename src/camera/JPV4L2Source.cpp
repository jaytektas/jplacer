// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPV4L2Source.h"

#include "JPPixels.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

inline namespace jf {

namespace {

// jplacer's name -> the control that turns it automatic (and its on and
// off values), and the one that sets it by hand.
struct Control { const char* name; uint32_t autoId; int autoOn, autoOff; uint32_t valueId; };
const Control kControls[] = {
    { "exposure",      V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_APERTURE_PRIORITY, V4L2_EXPOSURE_MANUAL, V4L2_CID_EXPOSURE_ABSOLUTE },
    { "white-balance", V4L2_CID_AUTO_WHITE_BALANCE, 1, 0, V4L2_CID_WHITE_BALANCE_TEMPERATURE },
    { "focus",         V4L2_CID_FOCUS_AUTO, 1, 0, V4L2_CID_FOCUS_ABSOLUTE },
    { "gain",          V4L2_CID_AUTOGAIN, 1, 0, V4L2_CID_GAIN },
    { "brightness",    V4L2_CID_AUTOBRIGHTNESS, 1, 0, V4L2_CID_BRIGHTNESS },
    { "hue",           V4L2_CID_HUE_AUTO, 1, 0, V4L2_CID_HUE },
    { "contrast",               0, 0, 0, V4L2_CID_CONTRAST },
    { "saturation",             0, 0, 0, V4L2_CID_SATURATION },
    { "gamma",                  0, 0, 0, V4L2_CID_GAMMA },
    { "sharpness",              0, 0, 0, V4L2_CID_SHARPNESS },
    { "backlight-compensation", 0, 0, 0, V4L2_CID_BACKLIGHT_COMPENSATION },
    { "power-line-frequency",   0, 0, 0, V4L2_CID_POWER_LINE_FREQUENCY },
    { "zoom",                   0, 0, 0, V4L2_CID_ZOOM_ABSOLUTE },
};

// How many buffers the driver fills ahead: enough that a slow decode does
// not drop the stream, few enough that the picture is not old.
constexpr unsigned kBuffers = 3;

int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do r = ::ioctl(fd, request, arg);
    while (r == -1 && errno == EINTR);
    return r;
}

std::string fourccName(uint32_t f) {
    return std::string{ char(f & 0xff), char((f >> 8) & 0xff), char((f >> 16) & 0xff), char((f >> 24) & 0xff) };
}

uint32_t fourccOf(const std::string& s) {
    return s.size() == 4 ? v4l2_fourcc(s[0], s[1], s[2], s[3]) : 0;
}

bool supported(uint32_t f) { return f == V4L2_PIX_FMT_MJPEG || f == V4L2_PIX_FMT_YUYV; }

std::string sysName(const std::string& dev) {
    std::ifstream in("/sys/class/video4linux/" + std::filesystem::path(dev).filename().string() + "/name");
    std::string n;
    std::getline(in, n);
    return n;
}

} // namespace

JPV4L2Source::JPV4L2Source(std::string name, JJson controls)
    : m_name(std::move(name)), m_controls(std::move(controls)) {}

JPV4L2Source::~JPV4L2Source() { close(); }

std::string JPV4L2Source::findDevice(const std::string& name) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<std::string> devices;
    for (const auto& e : fs::directory_iterator("/dev", ec))
        if (e.path().filename().string().rfind("video", 0) == 0) devices.push_back(e.path().string());
    std::sort(devices.begin(), devices.end());
    for (const std::string& dev : devices) {
        // By its name, or by its node itself (an OpenPnP OpenCvCamera's index, /dev/video<index>).
        if (sysName(dev) != name && dev != name) continue;
        // A UVC camera makes two nodes: pictures, and metadata. Take the one
        // that captures pictures.
        const int fd = ::open(dev.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        v4l2_capability cap{};
        const bool video = xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0
                        && ((cap.device_caps ? cap.device_caps : cap.capabilities) & V4L2_CAP_VIDEO_CAPTURE)
                        && ((cap.device_caps ? cap.device_caps : cap.capabilities) & V4L2_CAP_STREAMING);
        ::close(fd);
        if (video) return dev;
    }
    return {};
}

std::vector<std::string> JPV4L2Source::deviceNames() {
    namespace fs = std::filesystem;
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/video4linux", ec)) {
        const std::string n = sysName(e.path().filename().string());
        if (!n.empty() && std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool JPV4L2Source::open(std::string& error) {
    m_path = findDevice(m_name);
    if (m_path.empty()) {
        error = "no camera called '" + m_name + "' is plugged in";
        return false;
    }
    m_fd = ::open(m_path.c_str(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        error = m_path + ": " + std::strerror(errno);
        return false;
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "opened " << describe();
    return true;
}

void JPV4L2Source::unmap() {
    for (Buffer& b : m_buffers) if (b.data) ::munmap(b.data, b.size);
    m_buffers.clear();
}

void JPV4L2Source::close() {
    if (m_fd < 0) return;
    if (m_streaming) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(m_fd, VIDIOC_STREAMOFF, &type);
        m_streaming = false;
    }
    unmap();
    ::close(m_fd);
    m_fd = -1;
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "closed " << describe();
}

std::vector<JPCaptureMode> JPV4L2Source::modes() const {
    std::vector<JPCaptureMode> out;
    if (m_fd < 0) return out;
    v4l2_fmtdesc fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (fmt.index = 0; xioctl(m_fd, VIDIOC_ENUM_FMT, &fmt) == 0; ++fmt.index) {
        if (!supported(fmt.pixelformat)) continue;
        v4l2_frmsizeenum size{};
        size.pixel_format = fmt.pixelformat;
        for (size.index = 0; xioctl(m_fd, VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
            if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) continue;
            v4l2_frmivalenum iv{};
            iv.pixel_format = fmt.pixelformat;
            iv.width  = size.discrete.width;
            iv.height = size.discrete.height;
            double best = 0;
            for (iv.index = 0; xioctl(m_fd, VIDIOC_ENUM_FRAMEINTERVALS, &iv) == 0; ++iv.index)
                if (iv.type == V4L2_FRMIVAL_TYPE_DISCRETE && iv.discrete.numerator)
                    best = std::max(best, double(iv.discrete.denominator) / iv.discrete.numerator);
            out.push_back({ fourccName(fmt.pixelformat), int(size.discrete.width), int(size.discrete.height), best });
        }
    }
    return out;
}

bool JPV4L2Source::start(const JPCaptureMode& mode, std::string& error) {
    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = uint32_t(mode.width);
    fmt.fmt.pix.height      = uint32_t(mode.height);
    fmt.fmt.pix.pixelformat = fourccOf(mode.format);
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;
    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) != 0) {
        error = describe() + ": mode " + mode.describe() + " refused: " + std::strerror(errno);
        return false;
    }
    m_fourcc = fmt.fmt.pix.pixelformat;
    m_width  = int(fmt.fmt.pix.width);
    m_height = int(fmt.fmt.pix.height);
    if (!supported(m_fourcc)) {
        error = describe() + ": delivers " + fourccName(m_fourcc) + ", which jplacer cannot read";
        return false;
    }
    if (mode.fps > 0) {
        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator   = 1000;
        parm.parm.capture.timeperframe.denominator = uint32_t(mode.fps * 1000);
        xioctl(m_fd, VIDIOC_S_PARM, &parm);   // a rate the device cannot do is rounded, not refused
    }

    v4l2_requestbuffers req{};
    req.count  = kBuffers;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(m_fd, VIDIOC_REQBUFS, &req) != 0 || req.count == 0) {
        error = describe() + ": no capture buffers: " + std::strerror(errno);
        return false;
    }
    for (unsigned i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) != 0) { error = describe() + ": buffer query failed"; return false; }
        void* p = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);
        if (p == MAP_FAILED) { error = describe() + ": buffer map failed"; unmap(); return false; }
        m_buffers.push_back({ p, buf.length });
        if (xioctl(m_fd, VIDIOC_QBUF, &buf) != 0) { error = describe() + ": buffer queue failed"; unmap(); return false; }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fd, VIDIOC_STREAMON, &type) != 0) {
        error = describe() + ": would not start: " + std::strerror(errno);
        unmap();
        return false;
    }
    m_streaming = true;
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << describe() << ": " << fourccName(m_fourcc) << " "
                                                << m_width << "x" << m_height;
    applyControls();
    return true;
}

JJson JPV4L2Source::controls() const {
    JJson out = JJson::object();
    if (m_fd < 0) return out;
    for (const Control& c : kControls) {
        v4l2_queryctrl q{};
        q.id = c.valueId;
        if (xioctl(m_fd, VIDIOC_QUERYCTRL, &q) != 0 || (q.flags & V4L2_CTRL_FLAG_DISABLED)) continue;
        v4l2_control v{};
        v.id = c.valueId;
        if (xioctl(m_fd, VIDIOC_G_CTRL, &v) != 0) continue;
        JJson& r = out[c.name];
        r["value"] = v.value;
        r["min"] = q.minimum;
        r["max"] = q.maximum;
        r["default"] = q.default_value;
        if (c.autoId) {
            v4l2_queryctrl qa{};
            qa.id = c.autoId;
            v4l2_control a{};
            a.id = c.autoId;
            if (xioctl(m_fd, VIDIOC_QUERYCTRL, &qa) == 0 && !(qa.flags & V4L2_CTRL_FLAG_DISABLED)
                && xioctl(m_fd, VIDIOC_G_CTRL, &a) == 0)
                r["auto"] = a.value != c.autoOff;
        }
    }
    return out;
}

void JPV4L2Source::applyControls() {
    auto set = [this](const char* name, uint32_t id, int value) {
        v4l2_control c{};
        c.id = id;
        c.value = value;
        if (xioctl(m_fd, VIDIOC_S_CTRL, &c) != 0)
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << describe() << ": " << name << " not set to " << value << " ("
                                                        << std::strerror(errno) << ")";
        else
            JLOGC(JPlacerLog::kCamera, JLogLevel::Debug) << describe() << ": " << name << " set to " << value;
    };
    auto has = [this](uint32_t id) {
        v4l2_queryctrl q{};
        q.id = id;
        return xioctl(m_fd, VIDIOC_QUERYCTRL, &q) == 0 && !(q.flags & V4L2_CTRL_FLAG_DISABLED);
    };
    // Automatic or not first: a value is refused while its control is
    // automatic. A camera with no automatic mode for it is set by hand anyway.
    for (const Control& c : kControls) {
        const JJson& want = m_controls[c.name];
        if (!want.isObject() || !want["auto"].isBool()) continue;
        const bool automatic = want["auto"].boolean();
        if (c.autoId && has(c.autoId))
            set(c.name, c.autoId, automatic ? c.autoOn : c.autoOff);
        else if (automatic)
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << describe() << ": has no automatic " << c.name;
    }
    for (const Control& c : kControls) {
        const JJson& want = m_controls[c.name];
        if (want.isObject() && !want["auto"].boolean() && want["value"].isNumber()) set(c.name, c.valueId, int(want["value"].number()));
    }
}

bool JPV4L2Source::grab(JPFrame& frame, int timeoutMs, std::string& error) {
    pollfd p{ m_fd, POLLIN, 0 };
    const int r = ::poll(&p, 1, timeoutMs);
    if (r == 0) return false;   // nothing yet
    if (r < 0 || (p.revents & (POLLERR | POLLHUP))) {
        error = describe() + ": the camera stopped (unplugged?)";
        return false;
    }
    v4l2_buffer buf{};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (xioctl(m_fd, VIDIOC_DQBUF, &buf) != 0) {
        if (errno == EAGAIN) return false;
        error = describe() + ": " + std::strerror(errno);
        return false;
    }
    bool ok = true;
    const auto* data = static_cast<const uint8_t*>(m_buffers[buf.index].data);
    if (m_fourcc == V4L2_PIX_FMT_YUYV) {
        JPPixels::yuyvToRgba(data, m_width, m_height, frame.rgba);
        frame.width  = m_width;
        frame.height = m_height;
    } else {
        ok = JPPixels::jpegToRgba(data, buf.bytesused, frame.width, frame.height, frame.rgba, error);
        if (!ok) error = describe() + ": a frame could not be decoded (" + error + ")";
    }
    frame.sequence = ++m_sequence;
    // The driver's timestamp is the monotonic clock steady_clock reads on
    // Linux; a driver without one gets the time it arrived.
    if ((buf.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC)
        frame.captured = std::chrono::steady_clock::time_point(
            std::chrono::seconds(buf.timestamp.tv_sec) + std::chrono::microseconds(buf.timestamp.tv_usec));
    else
        frame.captured = std::chrono::steady_clock::now();
    xioctl(m_fd, VIDIOC_QBUF, &buf);
    return ok;
}

std::string JPV4L2Source::describe() const {
    return m_name + (m_path.empty() ? std::string() : " (" + m_path + ")");
}

} // inline namespace jf
