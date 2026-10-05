// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// OpenPnP's OnvifIPCamera against a stand-in ONVIF camera on this machine:
// the password sent as a WS-Security digest, the first JPEG profile found,
// set to the preferred resolution (else the largest) at the best quality and
// fastest rate offered, and its snapshots fetched and resized.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "camera/JPOnvif.h"
#include "camera/JPOnvifSource.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace jf;

namespace {

std::string between(const std::string& s, const std::string& a, const std::string& b) {
    const size_t i = s.find(a);
    if (i == std::string::npos) return {};
    const size_t j = s.find(b, i + a.size());
    return j == std::string::npos ? std::string() : s.substr(i + a.size(), j - i - a.size());
}

std::string soap(const std::string& body) {
    return "<?xml version=\"1.0\"?><env:Envelope xmlns:env=\"http://www.w3.org/2003/05/soap-envelope\" "
           "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
           "xmlns:tt=\"http://www.onvif.org/ver10/schema\"><env:Body>" + body + "</env:Body></env:Envelope>";
}

// A camera answering one request per connection until told to stop.
struct Camera {
    int                      port = 0, listener = -1;
    std::thread              thread;
    std::atomic<bool>        stop { false };
    std::vector<std::string> requests;
    std::string              jpeg, setConfig;

    void start() {
        listener = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a {};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof a);
        ::listen(listener, 8);
        socklen_t len = sizeof a;
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
        thread = std::thread([this] { serve(); });
    }
    void serve() {
        while (!stop) {
            const int c = ::accept(listener, nullptr, nullptr);
            if (c < 0) break;
            std::string got;
            char buf[8192];
            for (;;) {
                const ssize_t n = ::recv(c, buf, sizeof buf, 0);
                if (n <= 0) break;
                got.append(buf, size_t(n));
                const size_t head = got.find("\r\n\r\n");
                if (head == std::string::npos) continue;
                const std::string cl = between(got, "Content-Length: ", "\r\n");
                if (got.size() >= head + 4 + size_t(cl.empty() ? 0 : std::stoi(cl))) break;
            }
            requests.push_back(got);
            std::string type = "application/soap+xml", body;
            if (got.rfind("GET /snap", 0) == 0) {
                type = "image/jpeg";
                body = jpeg;
            } else if (got.find("GetDeviceInformation") != std::string::npos)
                body = soap("<tds:GetDeviceInformationResponse><tds:Manufacturer>Acme</tds:Manufacturer><tds:Model>Eye 3"
                            "</tds:Model><tds:SerialNumber>42</tds:SerialNumber><tds:FirmwareVersion>1.0</tds:FirmwareVersion>"
                            "</tds:GetDeviceInformationResponse>");
            else if (got.find("GetCapabilities") != std::string::npos)
                body = soap("<tds:GetCapabilitiesResponse><tds:Capabilities><tt:Media><tt:XAddr>http://127.0.0.1:"
                            + std::to_string(port) + "/onvif/Media</tt:XAddr></tt:Media></tds:Capabilities>"
                            "</tds:GetCapabilitiesResponse>");
            else if (got.find("GetProfiles") != std::string::npos)
                body = soap("<trt:GetProfilesResponse>"
                            "<trt:Profiles token=\"h264\"><tt:Name>main</tt:Name><tt:VideoEncoderConfiguration token=\"v0\">"
                            "<tt:Encoding>H264</tt:Encoding></tt:VideoEncoderConfiguration></trt:Profiles>"
                            "<trt:Profiles token=\"snap\"><tt:Name>jpeg</tt:Name><tt:VideoEncoderConfiguration token=\"v1\">"
                            "<tt:Name>v1</tt:Name><tt:UseCount>1</tt:UseCount><tt:Encoding>JPEG</tt:Encoding>"
                            "<tt:Resolution><tt:Width>320</tt:Width><tt:Height>240</tt:Height></tt:Resolution>"
                            "<tt:Quality>3</tt:Quality><tt:RateControl><tt:FrameRateLimit>5</tt:FrameRateLimit>"
                            "<tt:EncodingInterval>2</tt:EncodingInterval><tt:BitrateLimit>512</tt:BitrateLimit></tt:RateControl>"
                            "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address>"
                            "</tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                            "<tt:SessionTimeout>PT60S</tt:SessionTimeout></tt:VideoEncoderConfiguration></trt:Profiles>"
                            "</trt:GetProfilesResponse>");
            else if (got.find("GetVideoEncoderConfigurationOptions") != std::string::npos)
                body = soap("<trt:GetVideoEncoderConfigurationOptionsResponse><trt:Options>"
                            "<tt:QualityRange><tt:Min>1</tt:Min><tt:Max>10</tt:Max></tt:QualityRange><tt:JPEG>"
                            "<tt:ResolutionsAvailable><tt:Width>640</tt:Width><tt:Height>480</tt:Height></tt:ResolutionsAvailable>"
                            "<tt:ResolutionsAvailable><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:ResolutionsAvailable>"
                            "<tt:ResolutionsAvailable><tt:Width>1280</tt:Width><tt:Height>720</tt:Height></tt:ResolutionsAvailable>"
                            "<tt:FrameRateRange><tt:Min>1</tt:Min><tt:Max>25</tt:Max></tt:FrameRateRange>"
                            "<tt:EncodingIntervalRange><tt:Min>1</tt:Min><tt:Max>5</tt:Max></tt:EncodingIntervalRange></tt:JPEG>"
                            "<tt:Extension><tt:JPEG><tt:BitrateRange><tt:Min>64</tt:Min><tt:Max>8192</tt:Max></tt:BitrateRange>"
                            "</tt:JPEG></tt:Extension></trt:Options></trt:GetVideoEncoderConfigurationOptionsResponse>");
            else if (got.find("SetVideoEncoderConfiguration") != std::string::npos) {
                setConfig = got;
                body = soap("<trt:SetVideoEncoderConfigurationResponse/>");
            } else if (got.find("GetSnapshotUri") != std::string::npos)
                body = soap("<trt:GetSnapshotUriResponse><trt:MediaUri><tt:Uri>http://127.0.0.1:" + std::to_string(port)
                            + "/snap.jpg</tt:Uri></trt:MediaUri></trt:GetSnapshotUriResponse>");
            const std::string answer = "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: "
                                     + std::to_string(body.size()) + "\r\n\r\n" + body;
            (void)::send(c, answer.data(), answer.size(), MSG_NOSIGNAL);
            ::close(c);
        }
    }
    void finish() {
        stop = true;
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        thread.join();
    }
};

} // namespace

int main() {
    // The digest, as WS-Security's UsernameToken profile has it.
    {
        const std::string h = JPOnvif::security("admin", "secret", "0123456789abcdef", "2026-10-06T00:00:00Z");
        assert(h.find(">P7vWBdCvLlBDSCdc5zt2PKUk7EM=</Password>") != std::string::npos);
        assert(h.find(">MDEyMzQ1Njc4OWFiY2RlZg==</Nonce>") != std::string::npos);
        assert(h.find("<Username>admin</Username>") != std::string::npos);
    }
    Camera cam;
    {
        std::ifstream in(std::string(JPLACER_TESTDATA_DIR) + "/mjpg-frame.jpg", std::ios::binary);
        cam.jpeg.assign(std::istreambuf_iterator<char>(in), {});
        assert(!cam.jpeg.empty());
    }
    cam.start();
    const std::string host = "127.0.0.1:" + std::to_string(cam.port);
    // The largest, unless one is preferred.
    {
        JPOnvif o(host, "admin", "secret");
        std::string why;
        const bool ok = o.open("", why);
        if (!ok) std::fprintf(stderr, "why: %s\n", why.c_str());
        assert(ok && o.chosen().text() == "1920x1080" && o.resolutions().size() == 3);
        assert(o.describe() == "Acme Eye 3");
        assert(o.snapshotUri() == "http://127.0.0.1:" + std::to_string(cam.port) + "/snap.jpg");
        assert(JPOnvif::knownResolutions(host).size() == 3);
        // Each call signed; the media calls to the media service it named.
        for (const std::string& r : cam.requests) assert(r.find("PasswordDigest") != std::string::npos);
        assert(cam.requests.back().rfind("POST /onvif/Media", 0) == 0);
        // The JPEG profile set as asked, the rest of it as it was.
        const std::string& s = cam.setConfig;
        assert(s.find("<Configuration token=\"v1\">") != std::string::npos);
        assert(s.find("<tt:Width>1920</tt:Width><tt:Height>1080</tt:Height>") != std::string::npos);
        assert(s.find("<tt:Quality>10</tt:Quality>") != std::string::npos);
        assert(s.find("<tt:FrameRateLimit>25</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval>"
                      "<tt:BitrateLimit>8192</tt:BitrateLimit>") != std::string::npos);
        assert(s.find("<tt:IPv4Address>0.0.0.0</tt:IPv4Address>") != std::string::npos);
        assert(s.find("<tt:SessionTimeout>PT60S</tt:SessionTimeout>") != std::string::npos);
        assert(s.find("<ForcePersistence>true</ForcePersistence>") != std::string::npos);
    }
    {
        JPOnvif o(host, "", "");
        std::string why;
        assert(o.open("1280x720", why) && o.chosen().text() == "1280x720");
        assert(cam.requests.back().find("PasswordDigest") == std::string::npos);   // no user, not signed
    }
    // Its pictures: the snapshot, resized to the target size.
    {
        JPOnvifSource::Settings s;
        s.host = host;
        s.resizeWidth = 4;
        s.resizeHeight = 3;
        s.fps = 0;
        JPOnvifSource src("Top", s);
        std::string why;
        assert(src.open(why));
        JPFrame f;
        assert(src.grab(f, 2000, why) && f.width == 4 && f.height == 3 && f.rgba.size() == 4 * 3 * 4);
        assert(src.modes().front().width == 4);
    }
    cam.finish();
    // Resizing: each pixel the mean of what it covers.
    {
        std::vector<uint8_t> in = { 0, 0, 0, 255, 100, 100, 100, 255, 200, 200, 200, 255, 50, 50, 50, 255 }, out;
        JPOnvifSource::resize(in, 2, 2, 1, 1, out);
        assert(out.size() == 4 && out[0] == 88 && out[3] == 255);
    }
    return 0;
}
