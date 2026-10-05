// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's MjpgCaptureCamera: the pictures taken from an HTTP stream of
// JPEGs (each from its start to its end, the newest when several have come),
// from a server on this machine.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPMjpgSource.h"

#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace jf;

int main() {
    // The frame scan: what is before and between pictures dropped, a picture still coming kept.
    {
        std::vector<uint8_t> b { 1, 2, 0xFF, 0xD8, 7, 0xFF, 0xD9, 9, 0xFF, 0xD8, 8, 8, 0xFF, 0xD9, 0xFF, 0xD8, 5 };
        std::vector<uint8_t> jpeg;
        assert(JPMjpgSource::takeJpeg(b, jpeg));
        assert((jpeg == std::vector<uint8_t> { 0xFF, 0xD8, 8, 8, 0xFF, 0xD9 }));   // the newest
        assert((b == std::vector<uint8_t> { 0xFF, 0xD8, 5 }));
        assert(!JPMjpgSource::takeJpeg(b, jpeg));
    }
    std::ifstream in(std::string(JPLACER_TESTDATA_DIR) + "/mjpg-frame.jpg", std::ios::binary);
    const std::string jpg((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    assert(!jpg.empty());

    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::listen(listener, 1) == 0);
    socklen_t len = sizeof a;
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
    std::string request;
    std::thread server([&] {
        const int c = ::accept(listener, nullptr, nullptr);
        char buf[1024];
        const ssize_t n = ::recv(c, buf, sizeof buf, 0);
        request.assign(buf, size_t(n > 0 ? n : 0));
        std::string out = "HTTP/1.0 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
        for (int i = 0; i < 2; ++i)
            out += "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " + std::to_string(jpg.size()) + "\r\n\r\n" + jpg + "\r\n";
        ::send(c, out.data(), out.size(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ::close(c);
    });
    JPMjpgSource cam("ip", "http://127.0.0.1:" + std::to_string(ntohs(a.sin_port)) + "/stream", 8, 6, 2000);
    std::string error;
    assert(cam.open(error));
    JPFrame f;
    assert(cam.grab(f, 2000, error));
    assert(f.width == 8 && f.height == 6 && f.rgba.size() == 8 * 6 * 4);
    assert(f.rgba[0] > 200 && f.rgba[1] < 50 && f.rgba[2] < 50);   // red
    server.join();
    ::close(listener);
    assert(request.rfind("GET /stream HTTP/1.0\r\n", 0) == 0);
    // An address that is not http://: refused, saying so.
    JPMjpgSource bad("ip", "rtsp://camera/stream", 8, 6, 100);
    assert(!bad.open(error) && error.find("http://") != std::string::npos);
    return 0;
}
