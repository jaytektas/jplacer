// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPMediaFoundationSource.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <strmif.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>

inline namespace jf {

namespace {

constexpr DWORD kStream = DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
// Windows keeps exposure as log2 of seconds (-6: 1/64 s); jplacer as Video4Linux does, in 100 µs.
constexpr double kExposureUnitsPerSecond = 10000.0;

// A COM pointer let go when it goes.
template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** put() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::string utf8(const wchar_t* w) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(std::max(0, n - 1)), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string hr(HRESULT h) {
    char text[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(h), 0, text, sizeof text, nullptr);
    std::string s = text;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '.')) s.pop_back();
    char code[16];
    std::snprintf(code, sizeof code, "0x%08lx", static_cast<unsigned long>(h));
    return s.empty() ? std::string(code) : s + " (" + code + ")";
}

// Media Foundation started once for the whole run.
void startMediaFoundation() {
    static std::once_flag once;
    std::call_once(once, [] { MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET); });
}

// A format's name as Video4Linux names it (YUY2 is its YUYV), else its four letters.
std::string formatName(const GUID& subtype) {
    if (subtype == MFVideoFormat_YUY2) return "YUYV";
    if (subtype == MFVideoFormat_MJPG) return "MJPG";
    if (subtype == MFVideoFormat_RGB24) return "RGB3";
    if (subtype == MFVideoFormat_RGB32) return "RGB4";
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i) s[size_t(i)] = char((subtype.Data1 >> (8 * i)) & 0xff);
    return s;
}

// The capture devices present, each by its name, the second of two alike "Name #2"; `activate` given the one
// named `want` (null: none).
std::vector<std::string> enumerate(const std::string& want, IMFActivate** activate) {
    std::vector<std::string> names;
    Com<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(attributes.put(), 1))) return names;
    attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attributes.p, &devices, &count))) return names;
    std::map<std::string, int> seen;
    for (UINT32 i = 0; i < count; ++i) {
        wchar_t* w = nullptr;
        UINT32 length = 0;
        std::string name;
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &w, &length))) {
            name = utf8(w);
            CoTaskMemFree(w);
        }
        if (const int n = ++seen[name]; n > 1) name += " #" + std::to_string(n);
        names.push_back(name);
        if (activate && !*activate && name == want) {
            *activate = devices[i];
            continue;   // kept (released by the caller)
        }
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return names;
}

// Receives the pictures the reader reads (asynchronously): the newest kept, waited for by grab.
class ReaderCallback : public IMFSourceReaderCallback {
public:
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_IUnknown || iid == __uuidof(IMFSourceReaderCallback)) {
            *out = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ULONG(++m_refs); }
    STDMETHODIMP_(ULONG) Release() override {
        const long n = --m_refs;
        if (n == 0) delete this;
        return ULONG(n);
    }
    STDMETHODIMP OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
        {
            std::lock_guard lk(m_mutex);
            if (FAILED(status)) m_error = "the camera stopped: " + hr(status);
            else if (flags & MF_SOURCE_READERF_ENDOFSTREAM) m_error = "the camera stopped (unplugged?)";
            else if (sample) {
                if (m_sample) m_sample->Release();
                m_sample = sample;
                m_sample->AddRef();
                ++m_sequence;
            }
        }
        m_ready.notify_all();
        // The next asked for at once: the newest is always on its way (not once the reader is let go).
        std::lock_guard rl(m_readerMutex);
        if (SUCCEEDED(status) && !(flags & MF_SOURCE_READERF_ENDOFSTREAM) && m_reader && !m_stopping)
            m_reader->ReadSample(kStream, 0, nullptr, nullptr, nullptr, nullptr);
        return S_OK;
    }
    STDMETHODIMP OnFlush(DWORD) override { return S_OK; }
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }

    // The reader it asks for the next of (not held: the reader holds this); set and cleared under m_readerMutex.
    IMFSourceReader* m_reader = nullptr;
    std::mutex m_readerMutex;
    std::atomic<bool> m_stopping { false };
    std::mutex m_mutex;
    std::condition_variable m_ready;
    IMFSample* m_sample = nullptr;   // the newest picture, held
    uint64_t m_sequence = 0;
    std::string m_error;

private:
    ~ReaderCallback() {
        if (m_sample) m_sample->Release();
    }
    std::atomic<long> m_refs { 1 };
};

// A control: Windows' video amp (IAMVideoProcAmp) or camera control (IAMCameraControl) property, by jplacer's name.
struct Control {
    const char* name;
    bool        camera;     // IAMCameraControl, else IAMVideoProcAmp
    long        property;
    bool        exposure;   // log2 seconds, given in 100 µs
};
const Control kControls[] = {
    { "exposure", true, CameraControl_Exposure, true },
    { "focus", true, CameraControl_Focus, false },
    { "zoom", true, CameraControl_Zoom, false },
    { "white-balance", false, VideoProcAmp_WhiteBalance, false },
    { "gain", false, VideoProcAmp_Gain, false },
    { "brightness", false, VideoProcAmp_Brightness, false },
    { "contrast", false, VideoProcAmp_Contrast, false },
    { "saturation", false, VideoProcAmp_Saturation, false },
    { "hue", false, VideoProcAmp_Hue, false },
    { "gamma", false, VideoProcAmp_Gamma, false },
    { "sharpness", false, VideoProcAmp_Sharpness, false },
    { "backlight-compensation", false, VideoProcAmp_BacklightCompensation, false },
};

// Exposure each way between Windows' (log2 seconds) and jplacer's (100 µs).
double exposureOut(long log2Seconds) { return std::round(std::pow(2.0, double(log2Seconds)) * kExposureUnitsPerSecond); }
long exposureIn(double units) { return long(std::lround(std::log2(std::max(units, 1.0) / kExposureUnitsPerSecond))); }

} // namespace

struct JPMediaFoundationSource::Impl {
    bool comStarted = false;
    Com<IMFMediaSource> source;
    Com<IMFSourceReader> reader;
    ReaderCallback* callback = nullptr;   // held (one reference)
    Com<IAMVideoProcAmp> videoAmp;
    Com<IAMCameraControl> cameraControl;
    std::vector<JPCaptureMode> modes;
    std::vector<DWORD> modeTypes;   // each mode's native media type, by its index
    int width = 0, height = 0;
    LONG stride = 0;
    uint64_t taken = 0;   // the picture last given
    bool streaming = false;

    // A control's range, value and whether it is automatic; false when the device has none.
    bool read(const Control& c, long& min, long& max, long& step, long& def, long& caps, long& value, long& flags) const {
        if (c.camera) {
            if (!cameraControl) return false;
            return SUCCEEDED(cameraControl->GetRange(c.property, &min, &max, &step, &def, &caps))
                && SUCCEEDED(cameraControl->Get(c.property, &value, &flags));
        }
        if (!videoAmp) return false;
        return SUCCEEDED(videoAmp->GetRange(c.property, &min, &max, &step, &def, &caps))
            && SUCCEEDED(videoAmp->Get(c.property, &value, &flags));
    }
    HRESULT write(const Control& c, long value, bool automatic) const {
        if (c.camera) return cameraControl ? cameraControl->Set(c.property, value, automatic ? CameraControl_Flags_Auto : CameraControl_Flags_Manual)
                                           : E_NOINTERFACE;
        return videoAmp ? videoAmp->Set(c.property, value, automatic ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual)
                        : E_NOINTERFACE;
    }
};

JPMediaFoundationSource::JPMediaFoundationSource(std::string name, JJson controls)
    : m_name(std::move(name)), m_controls(std::move(controls)), m_impl(std::make_unique<Impl>()) {}

JPMediaFoundationSource::~JPMediaFoundationSource() { close(); }

std::string JPMediaFoundationSource::describe() const { return m_name + " (Media Foundation)"; }

std::vector<std::string> JPMediaFoundationSource::deviceNames() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    startMediaFoundation();
    std::vector<std::string> names = enumerate("", nullptr);
    if (SUCCEEDED(com)) CoUninitialize();
    return names;
}

bool JPMediaFoundationSource::open(std::string& error) {
    close();
    Impl& m = *m_impl;
    // Media Foundation's objects work in a multithreaded apartment on this (the camera's capture) thread.
    m.comStarted = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    startMediaFoundation();
    IMFActivate* activate = nullptr;
    const std::vector<std::string> present = enumerate(m_name, &activate);
    if (!activate) {
        std::string list;
        for (const std::string& n : present) list += (list.empty() ? "" : ", ") + n;
        error = m_name + ": no camera of that name is plugged in" + (list.empty() ? std::string(" (none is)") : " (there are: " + list + ")");
        close();
        return false;
    }
    const HRESULT a = activate->ActivateObject(IID_PPV_ARGS(m.source.put()));
    activate->Release();
    if (FAILED(a)) {
        error = m_name + ": cannot be opened: " + hr(a);
        close();
        return false;
    }
    // Pictures read as they come (the callback), decoded and turned to RGB by Media Foundation itself.
    m.callback = new ReaderCallback();
    Com<IMFAttributes> attributes;
    MFCreateAttributes(attributes.put(), 2);
    attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, m.callback);
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (const HRESULT r = MFCreateSourceReaderFromMediaSource(m.source.p, attributes.p, m.reader.put()); FAILED(r)) {
        error = m_name + ": cannot be read: " + hr(r);
        close();
        return false;
    }
    {
        std::lock_guard rl(m.callback->m_readerMutex);
        m.callback->m_reader = m.reader.p;
    }
    // Its own settings (Windows' camera controls), where it has them.
    m.source->QueryInterface(IID_PPV_ARGS(m.videoAmp.put()));
    m.source->QueryInterface(IID_PPV_ARGS(m.cameraControl.put()));
    // The formats it offers.
    for (DWORD i = 0;; ++i) {
        Com<IMFMediaType> type;
        if (FAILED(m.reader->GetNativeMediaType(kStream, i, type.put()))) break;
        GUID subtype {};
        UINT32 w = 0, h = 0, num = 0, den = 0;
        if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) || FAILED(MFGetAttributeSize(type.p, MF_MT_FRAME_SIZE, &w, &h))) continue;
        MFGetAttributeRatio(type.p, MF_MT_FRAME_RATE, &num, &den);
        JPCaptureMode mode { formatName(subtype), int(w), int(h), den ? double(num) / double(den) : 0.0 };
        bool known = false;
        for (const JPCaptureMode& k : m.modes)
            known = known || (k.format == mode.format && k.width == mode.width && k.height == mode.height && std::abs(k.fps - mode.fps) < 0.01);
        if (known) continue;
        m.modes.push_back(mode);
        m.modeTypes.push_back(i);
    }
    if (m.modes.empty()) {
        error = m_name + ": offers no picture format";
        close();
        return false;
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << describe() << ": " << m.modes.size() << " format(s)";
    return true;
}

void JPMediaFoundationSource::close() {
    Impl& m = *m_impl;
    if (m.callback) {
        m.callback->m_stopping = true;
        std::lock_guard rl(m.callback->m_readerMutex);
        m.callback->m_reader = nullptr;   // a picture coming now asks for no next
    }
    if (m.reader && m.streaming) m.reader->Flush(kStream);
    m.streaming = false;
    m.videoAmp.reset();
    m.cameraControl.reset();
    m.reader.reset();
    if (m.source) m.source->Shutdown();
    m.source.reset();
    if (m.callback) m.callback->Release();
    m.callback = nullptr;
    m.modes.clear();
    m.modeTypes.clear();
    m.width = m.height = 0;
    if (m.comStarted) CoUninitialize();
    m.comStarted = false;
}

std::vector<JPCaptureMode> JPMediaFoundationSource::modes() const { return m_impl->modes; }

bool JPMediaFoundationSource::start(const JPCaptureMode& mode, std::string& error) {
    Impl& m = *m_impl;
    if (!m.reader) {
        error = describe() + ": not open";
        return false;
    }
    // The format asked for: its kind and size, the nearest rate.
    int chosen = -1;
    for (size_t i = 0; i < m.modes.size(); ++i) {
        const JPCaptureMode& o = m.modes[i];
        if (o.format != mode.format || o.width != mode.width || o.height != mode.height) continue;
        if (chosen < 0 || std::abs(o.fps - mode.fps) < std::abs(m.modes[size_t(chosen)].fps - mode.fps)) chosen = int(i);
    }
    if (chosen < 0) {
        error = describe() + ": does not offer " + mode.describe();
        return false;
    }
    Com<IMFMediaType> native;
    if (const HRESULT r = m.reader->GetNativeMediaType(kStream, m.modeTypes[size_t(chosen)], native.put()); FAILED(r)
        || FAILED(m.reader->SetCurrentMediaType(kStream, nullptr, native.p))) {
        error = describe() + ": cannot be set to " + mode.describe();
        return false;
    }
    // Given as RGB (BGRA in memory), whatever it sends.
    Com<IMFMediaType> out;
    MFCreateMediaType(out.put());
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (const HRESULT r = m.reader->SetCurrentMediaType(kStream, nullptr, out.p); FAILED(r)) {
        error = describe() + ": its pictures cannot be turned to RGB: " + hr(r);
        return false;
    }
    Com<IMFMediaType> current;
    m.reader->GetCurrentMediaType(kStream, current.put());
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(current.p, MF_MT_FRAME_SIZE, &w, &h);
    m.width = int(w);
    m.height = int(h);
    UINT32 stride = 0;
    m.stride = SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? LONG(INT32(stride)) : LONG(m.width * 4);
    applyControls();
    m.callback->m_stopping = false;
    if (const HRESULT r = m.reader->ReadSample(kStream, 0, nullptr, nullptr, nullptr, nullptr); FAILED(r)) {
        error = describe() + ": cannot start: " + hr(r);
        return false;
    }
    m.streaming = true;
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << describe() << ": " << m.modes[size_t(chosen)].describe();
    return true;
}

bool JPMediaFoundationSource::grab(JPFrame& frame, int timeoutMs, std::string& error) {
    Impl& m = *m_impl;
    if (!m.callback || !m.streaming) {
        error = describe() + ": not started";
        return false;
    }
    IMFSample* sample = nullptr;
    {
        std::unique_lock lk(m.callback->m_mutex);
        const bool came = m.callback->m_ready.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] {
            return m.callback->m_sequence != m.taken || !m.callback->m_error.empty();
        });
        if (!m.callback->m_error.empty()) {
            error = describe() + ": " + m.callback->m_error;
            return false;
        }
        if (!came) return false;   // nothing yet
        m.taken = m.callback->m_sequence;
        sample = m.callback->m_sample;
        sample->AddRef();
    }
    Com<IMFMediaBuffer> buffer;
    const HRESULT got = sample->ConvertToContiguousBuffer(buffer.put());
    sample->Release();
    if (FAILED(got)) {
        error = describe() + ": a picture could not be read: " + hr(got);
        return false;
    }
    // Its rows as they lie (a 2D buffer says where each is: bottom-up, its pitch negative), BGRA to RGBA.
    BYTE* first = nullptr;
    LONG pitch = m.stride;
    Com<IMF2DBuffer> rows;
    bool locked2d = SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(rows.put()))) && SUCCEEDED(rows->Lock2D(&first, &pitch));
    DWORD length = 0;
    if (!locked2d) {
        BYTE* start = nullptr;
        if (FAILED(buffer->Lock(&start, nullptr, &length))) {
            error = describe() + ": a picture could not be read";
            return false;
        }
        first = pitch < 0 ? start + size_t(-pitch) * size_t(m.height - 1) : start;
    }
    frame.width = m.width;
    frame.height = m.height;
    frame.rgba.resize(size_t(m.width) * size_t(m.height) * 4);
    for (int y = 0; y < m.height; ++y) {
        const BYTE* in = first + std::ptrdiff_t(y) * pitch;
        uint8_t* out = frame.rgba.data() + size_t(y) * size_t(m.width) * 4;
        for (int x = 0; x < m.width; ++x, in += 4, out += 4) {
            out[0] = in[2];
            out[1] = in[1];
            out[2] = in[0];
            out[3] = 255;
        }
    }
    if (locked2d) rows->Unlock2D();
    else buffer->Unlock();
    frame.captured = std::chrono::steady_clock::now();
    return true;
}

JJson JPMediaFoundationSource::controls() const {
    JJson out = JJson::object();
    for (const Control& c : kControls) {
        long min = 0, max = 0, step = 0, def = 0, caps = 0, value = 0, flags = 0;
        if (!m_impl->read(c, min, max, step, def, caps, value, flags)) continue;
        JJson& r = out[c.name];
        r["value"] = c.exposure ? exposureOut(value) : double(value);
        r["min"] = c.exposure ? exposureOut(min) : double(min);
        r["max"] = c.exposure ? exposureOut(max) : double(max);
        r["default"] = c.exposure ? exposureOut(def) : double(def);
        if (caps & VideoProcAmp_Flags_Auto) r["auto"] = (flags & VideoProcAmp_Flags_Auto) != 0;   // camera control's flags alike
    }
    return out;
}

void JPMediaFoundationSource::applyControls() {
    Impl& m = *m_impl;
    for (const Control& c : kControls) {
        const JJson& want = m_controls[c.name];
        if (!want.isObject() || (!want["auto"].isBool() && !want["value"].isNumber())) continue;
        long min = 0, max = 0, step = 0, def = 0, caps = 0, value = 0, flags = 0;
        if (!m.read(c, min, max, step, def, caps, value, flags)) {
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << describe() << ": has no " << c.name;
            continue;
        }
        const bool automatic = want["auto"].isBool() && want["auto"].boolean();
        if (automatic && !(caps & VideoProcAmp_Flags_Auto)) {
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << describe() << ": has no automatic " << c.name;
            continue;
        }
        // By hand: the value asked for, else what it has (switched to manual, keeping it).
        const long set = automatic ? value
                       : want["value"].isNumber() ? (c.exposure ? exposureIn(want["value"].number()) : long(std::lround(want["value"].number())))
                                                  : value;
        if (const HRESULT r = m.write(c, std::clamp(set, min, max), automatic); FAILED(r))
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << describe() << ": " << c.name << " not set: " << hr(r);
        else
            JLOGC(JPlacerLog::kCamera, JLogLevel::Debug) << describe() << ": " << c.name << (automatic ? " automatic" : " set to " + std::to_string(set));
    }
}

} // inline namespace jf
