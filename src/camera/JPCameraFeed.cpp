// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPImageTransform.h"
#include "JPCameraFeed.h"
#include "JPAutoTune.h"

#include "JPCaptureFactory.h"

#include "common/JPlacerLog.h"

#include <j/core/Log.h>

#include <chrono>
#include <thread>

inline namespace jf {

namespace {

// A frame's average brightness, from every 97th pixel (enough to tell a dark
// scene from a broken decode; cheap enough to log every frame when traced).
int meanBrightness(const JPFrame& f) {
    constexpr size_t kStride = 97 * 4;
    uint64_t sum = 0, n = 0;
    for (size_t i = 0; i + 2 < f.rgba.size(); i += kStride, ++n) sum += (f.rgba[i] + f.rgba[i + 1] + f.rgba[i + 2]) / 3;
    return n ? int(sum / n) : 0;
}

// How long one wait for a frame lasts before the thread checks whether it
// should stop: the time a stop can take, not a frame timeout.
constexpr int kGrabSliceMs = 100;
// A camera that is lost is looked for again this often (when it counts as
// lost is the camera's own: JPCameraConfig::Lost).
constexpr int kReconnectMs = 2000;

// A quick fingerprint of a picture: enough of its bytes to tell any two real
// ones apart.
uint64_t fingerprint(const std::vector<uint8_t>& bytes) {
    uint64_t h = 1469598103934665603ull;
    const size_t step = std::max<size_t>(1, bytes.size() / 4096);
    for (size_t i = 0; i < bytes.size(); i += step) h = (h ^ bytes[i]) * 1099511628211ull;
    return h;
}

} // namespace

JPCameraFeed::JPCameraFeed(JPCameraConfig config) : m_config(std::move(config)), m_balance(m_config.whiteBalance) {}

JPCameraFeed::~JPCameraFeed() { stop(); }

void JPCameraFeed::start() {
    if (m_running.exchange(true)) return;
    m_thread = std::thread(&JPCameraFeed::run, this);
}

void JPCameraFeed::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

JJson JPCameraFeed::deviceControls() const {
    std::lock_guard lk(m_mutex);
    return m_deviceControls;
}

bool JPCameraFeed::latestUnbalanced(JPFrame& out) const {
    std::lock_guard lk(m_mutex);
    const JPFrame& f = m_config.whiteBalance.neutral() ? m_latest : m_unbalanced;
    if (f.sequence == 0) return false;
    out = f;
    return true;
}

bool JPCameraFeed::latest(JPFrame& out, uint64_t have) const {
    std::lock_guard lk(m_mutex);
    if (m_latest.sequence == 0 || m_latest.sequence == have) return false;
    out = m_latest;
    return true;
}

std::optional<JPCaptureMode> JPCameraFeed::mode() const {
    std::lock_guard lk(m_mutex);
    return m_mode;
}

void JPCameraFeed::autoTune(int autoMs, std::function<void(std::optional<JJson>)> done) {
    std::lock_guard lk(m_mutex);
    m_tuneAsked = Tune { autoMs, std::move(done) };
}

void JPCameraFeed::run() {
    // A camera can drop off its bus (a stepper's noise on a USB cable) or
    // hang with no error: either way it is closed and opened again, by its
    // name, until it is back or the feed is stopped.
    std::string lastWhy;
    while (m_running) {
        std::string why;
        runSource(why);
        if (!m_running) break;
        if (why != lastWhy) {
            JLOGC(JPlacerLog::kCamera, JLogLevel::Warn) << m_config.name << ": " << why << "; trying again";
            lastWhy = why;
        }
        {
            std::lock_guard lk(m_mutex);
            m_lostWhy = why;
        }
        m_lost = true;
        onError.emit(why);
        for (int waited = 0; m_running && waited < kReconnectMs; waited += kGrabSliceMs)
            std::this_thread::sleep_for(std::chrono::milliseconds(kGrabSliceMs));
    }
    onRunning.emit(false);
}

void JPCameraFeed::runSource(std::string& why) {
    JPCaptureFactory::Context context;
    context.view = m_view;
    context.extras = m_extras;
    context.links = m_links;
    context.takeClaim = [this] { return m_claimed.exchange(false); };
    auto source = JPCaptureFactory::create(m_config.name, m_config.device, why, context);
    if (!source || !source->open(why)) return;
    const auto mode = JPCaptureFactory::choose(source->modes(), m_config.device);
    if (!mode) {
        why = source->describe() + " offers no picture format jplacer can read";
        return;
    }
    if (!source->start(*mode, why)) return;
    {
        std::lock_guard lk(m_mutex);
        m_mode = *mode;
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << m_config.name << ": " << mode->describe();
    {
        std::lock_guard lk(m_mutex);
        m_deviceControls = source->controls();
    }
    onRunning.emit(true);

    JPFrame frame;
    auto lastFrame = std::chrono::steady_clock::now();
    // The very same picture over and over: hung, for a camera that can freeze.
    const bool canFreeze = m_config.lost.samePictureS > 0 && source->canFreeze();
    const auto noPicture = std::chrono::seconds(std::max(1, m_config.lost.noPictureS));
    const auto samePicture = std::chrono::seconds(m_config.lost.samePictureS);
    uint64_t lastPrint = 0;
    auto changed = std::chrono::steady_clock::now();
    // Defaults, then Auto-Tune (autoTune): asked for, and under way.
    std::optional<Tune> tune;
    std::optional<JPAutoTune> tuning;
    while (m_running) {
        if (m_reapply.exchange(false)) {
            source->reapplyControls();
            std::lock_guard lk(m_mutex);
            m_deviceControls = source->controls();
        }
        if (!tune) {
            std::lock_guard lk(m_mutex);
            if (m_tuneAsked) {
                tune = std::move(m_tuneAsked);
                m_tuneAsked.reset();
            }
            if (tune) {
                tuning.emplace(tune->autoMs, kHoldMs);
                if (!tuning->start(*source, std::chrono::steady_clock::now())) {
                    tune->done(std::nullopt);
                    tune.reset();
                    tuning.reset();
                } else {
                    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << m_config.name << ": defaults set, auto-tuning";
                }
            }
        } else if (const auto tuned = tuning->step(*source, std::chrono::steady_clock::now())) {
            {
                std::lock_guard lk(m_mutex);
                m_deviceControls = source->controls();
            }
            JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << m_config.name << ": auto-tuned";
            tune->done(*tuned);
            tune.reset();
            tuning.reset();
        }
        std::string error;
        if (!source->grab(frame, kGrabSliceMs, error)) {
            if (!error.empty()) {
                why = error;
                break;
            }
            if (source->idle()) {
                lastFrame = std::chrono::steady_clock::now();
                continue;
            }
            if (std::chrono::steady_clock::now() - lastFrame > noPicture) {
                why = source->describe() + ": no picture for " + std::to_string(noPicture.count())
                    + " s (the camera may have hung)";
                break;
            }
            continue;
        }
        lastFrame = std::chrono::steady_clock::now();
        if (tuning) tuning->see(frame, lastFrame);   // Auto-Tune looks at the pictures as taken
        if (canFreeze) {
            const uint64_t print = fingerprint(frame.rgba);
            if (print != lastPrint) {
                lastPrint = print;
                changed = lastFrame;
            } else if (lastFrame - changed > samePicture) {
                why = source->describe() + ": the very same picture for " + std::to_string(samePicture.count())
                    + " s (the camera has hung)";
                break;
            }
        }
        m_lost = false;   // pictures again
        // OpenPnP's de-interlacing and cropping first, as it does them.
        if (m_config.deinterlace) JPImageTransform::deinterlace(frame);
        JPImageTransform::crop(frame, m_config.cropWidth, m_config.cropHeight);
        // Then OpenPnP's affine transforms, in its order.
        JPImageTransform::scale(frame, m_config.scaleWidth, m_config.scaleHeight);
        JPImageTransform::rotate(frame, m_config.rotation);
        JPImageTransform::offset(frame, m_config.offsetX, m_config.offsetY);
        JPImageTransform::flip(frame, m_config.flipX, m_config.flipY);
        frame.sequence = ++m_sequence;   // the feed's own count, unbroken when the camera is opened again
        JLOGC(JPlacerLog::kFrames, JLogLevel::Trace) << m_config.name << " frame " << frame.sequence << " "
                                                     << frame.width << "x" << frame.height << ", brightness "
                                                     << meanBrightness(frame) << "/255";
        // White balanced as the camera is set; the picture as taken kept beside it.
        JPFrame unbalanced;
        if (!m_config.whiteBalance.neutral()) {
            unbalanced = frame;
            m_balance.apply(frame);
        }
        {
            std::lock_guard lk(m_mutex);
            std::swap(m_latest, frame);
            if (!m_config.whiteBalance.neutral()) std::swap(m_unbalanced, unbalanced);
        }
        onFrame.emit(m_latest.sequence);
    }
    if (tune) tune->done(std::nullopt);   // stopped before the tuning was done
    source->close();
}

std::string JPCameraFeed::lostWhy() const {
    std::lock_guard lk(m_mutex);
    return m_lostWhy;
}

} // inline namespace jf
