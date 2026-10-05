// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <string>
#include <vector>

inline namespace jf {

// What OpenPnP's OnvifIPCamera asks of an ONVIF camera (SOAP over HTTP, the
// user's password sent as a WS-Security digest): its make and model, its
// first JPEG profile, the JPEG resolutions it offers, the profile set to one
// of them (the preferred one, else the largest) at its best quality, fastest
// rate and highest bitrate, and the address its snapshots are fetched from.
class JPOnvif {
public:
    struct Resolution {
        int width = 0, height = 0;
        std::string text() const { return std::to_string(width) + "x" + std::to_string(height); }
    };

    // `host`: "ip" or "ip:port".
    JPOnvif(std::string host, std::string username, std::string password);

    // Ask it all and set it up; false with why. `preferred`: "WxH", or empty
    // for the largest.
    bool open(const std::string& preferred, std::string& why);
    const std::string& snapshotUri() const { return m_snapshotUri; }
    const std::vector<Resolution>& resolutions() const { return m_resolutions; }
    const Resolution& chosen() const { return m_chosen; }
    const std::string& describe() const { return m_device; }

    // The JPEG resolutions last found on the camera at `host` (by any
    // JPOnvif in this run), for choosing among: empty until one opened it.
    static std::vector<Resolution> knownResolutions(const std::string& host);

    // The WS-Security header for `username` with `password`, the request
    // made at `created` (UTC, xsd:dateTime) with `nonce` (raw bytes):
    // PasswordDigest = Base64(SHA-1(nonce + created + password)).
    static std::string security(const std::string& username, const std::string& password, const std::string& nonce,
                                const std::string& created);
    static std::string base64(const std::string& bytes);

private:
    // One SOAP call: `body` posted to `url`; the answer's Body, or false with why.
    bool call(const std::string& url, const std::string& body, std::string& answer, std::string& why) const;

    std::string             m_host, m_username, m_password;
    std::string             m_snapshotUri, m_device;
    std::vector<Resolution> m_resolutions;
    Resolution              m_chosen;
};

} // inline namespace jf
