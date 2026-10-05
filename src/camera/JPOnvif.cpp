// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPOnvif.h"

#include "common/JPlacerLog.h"
#include "openpnp/JPXmlReader.h"

#include <j/core/Log.h>
#include <j/io/HttpClient.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <ctime>
#include <map>
#include <mutex>

inline namespace jf {

namespace {

constexpr int kSoapTimeoutMs = 10000;
constexpr int kNonceBytes = 16;

std::mutex                                                s_knownLock;
std::map<std::string, std::vector<JPOnvif::Resolution>>   s_known;

// An element's name without its namespace prefix ("tt:Width" is "Width").
std::string local(const std::string& name) {
    const size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

const JPXmlElement* childNamed(const JPXmlElement& e, const std::string& name) {
    for (const JPXmlElement& c : e.children)
        if (local(c.name) == name) return &c;
    return nullptr;
}

// The first element called `name` anywhere under `e` (e itself included).
const JPXmlElement* find(const JPXmlElement& e, const std::string& name) {
    if (local(e.name) == name) return &e;
    for (const JPXmlElement& c : e.children)
        if (const JPXmlElement* f = find(c, name)) return f;
    return nullptr;
}

std::string textOf(const JPXmlElement* e) {
    if (!e) return {};
    const size_t a = e->text.find_first_not_of(" \t\r\n"), b = e->text.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : e->text.substr(a, b - a + 1);
}

std::string escape(const std::string& s) {
    std::string out;
    for (char ch : s) {
        if (ch == '&') out += "&amp;";
        else if (ch == '<') out += "&lt;";
        else if (ch == '>') out += "&gt;";
        else if (ch == '"') out += "&quot;";
        else out += ch;
    }
    return out;
}

// An element written back in the ONVIF schema's namespace (tt:), as it came;
// called `as` instead, when given (the request's own element).
std::string write(const JPXmlElement& e, const std::string& as = {}) {
    const std::string name = as.empty() ? "tt:" + local(e.name) : as;
    std::string out = "<" + name;
    for (const std::string& a : e.attributeOrder)
        if (a.rfind("xmlns", 0) != 0) out += " " + local(a) + "=\"" + escape(e.attr(a)) + "\"";
    out += ">";
    if (e.children.empty()) out += escape(textOf(&e));
    for (const JPXmlElement& c : e.children) out += write(c);
    return out + "</" + name + ">";
}

// Set the text of `e`'s child `name` (found by its local name).
void setChild(JPXmlElement& e, const std::string& name, const std::string& value) {
    for (JPXmlElement& c : e.children)
        if (local(c.name) == name) {
            c.text = value;
            return;
        }
}

JPXmlElement* childNamed(JPXmlElement& e, const std::string& name) {
    for (JPXmlElement& c : e.children)
        if (local(c.name) == name) return &c;
    return nullptr;
}

} // namespace

JPOnvif::JPOnvif(std::string host, std::string username, std::string password)
    : m_host(std::move(host)), m_username(std::move(username)), m_password(std::move(password)) {}

std::string JPOnvif::base64(const std::string& bytes) {
    std::string out(4 * ((bytes.size() + 2) / 3) + 1, '\0');
    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                                  reinterpret_cast<const unsigned char*>(bytes.data()), int(bytes.size()));
    out.resize(size_t(n));
    return out;
}

std::string JPOnvif::security(const std::string& username, const std::string& password, const std::string& nonce,
                              const std::string& created) {
    const std::string joined = nonce + created + password;
    unsigned char sha[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(joined.data()), joined.size(), sha);
    const std::string digest = base64(std::string(reinterpret_cast<const char*>(sha), SHA_DIGEST_LENGTH));
    return "<Security s:mustUnderstand=\"1\" "
           "xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
           "<UsernameToken><Username>" + escape(username) + "</Username>"
           "<Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">"
           + digest + "</Password>"
           "<Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary\">"
           + base64(nonce) + "</Nonce>"
           "<Created xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\">"
           + created + "</Created></UsernameToken></Security>";
}

bool JPOnvif::call(const std::string& url, const std::string& body, std::string& answer, std::string& why) const {
    std::string header;
    if (!m_username.empty()) {
        std::string nonce(kNonceBytes, '\0');
        RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()), kNonceBytes);
        char created[32];
        const std::time_t now = std::time(nullptr);
        std::tm utc {};
        gmtime_r(&now, &utc);
        std::strftime(created, sizeof created, "%Y-%m-%dT%H:%M:%SZ", &utc);
        header = security(m_username, m_password, nonce, created);
    }
    const std::string envelope = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                                 "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
                                 "xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
                                 "<s:Header>" + header + "</s:Header><s:Body>" + body + "</s:Body></s:Envelope>";
    const JHttpResponse r = JHttpClient::postSync(url, envelope, "application/soap+xml; charset=utf-8", kSoapTimeoutMs);
    if (!r.error.empty()) {
        why = m_host + ": " + r.error;
        return false;
    }
    answer = r.text();
    if (!r.ok()) {
        // A SOAP fault says why.
        JPXmlElement root;
        std::string e;
        const std::string reason = JPXmlReader::parse(answer, root, e) ? textOf(find(root, "Text")) : std::string();
        why = m_host + ": " + (reason.empty() ? "HTTP " + std::to_string(r.status) : reason);
        return false;
    }
    return true;
}

bool JPOnvif::open(const std::string& preferred, std::string& why) {
    if (m_host.empty()) {
        why = "no camera IP set";
        return false;
    }
    const std::string device = "http://" + m_host + "/onvif/device_service";
    std::string text, e;
    JPXmlElement root;
    auto ask = [&](const std::string& url, const std::string& body) {
        root = {};
        if (!call(url, body, text, why)) return false;
        if (!JPXmlReader::parse(text, root, e)) {
            why = m_host + ": its answer is not XML (" + e + ")";
            return false;
        }
        return true;
    };
    // Who it is.
    if (!ask(device, "<GetDeviceInformation xmlns=\"http://www.onvif.org/ver10/device/wsdl\"/>")) return false;
    m_device = textOf(find(root, "Manufacturer")) + " " + textOf(find(root, "Model"));
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "ONVIF camera " << m_host << ": " << m_device << ", serial "
        << textOf(find(root, "SerialNumber")) << ", firmware " << textOf(find(root, "FirmwareVersion"));
    // Where its media service is.
    if (!ask(device, "<GetCapabilities xmlns=\"http://www.onvif.org/ver10/device/wsdl\"><Category>All</Category></GetCapabilities>"))
        return false;
    std::string media;
    if (const JPXmlElement* m = find(root, "Media")) media = textOf(childNamed(*m, "XAddr"));
    if (media.empty()) media = "http://" + m_host + "/onvif/media_service";
    const std::string trt = "xmlns=\"http://www.onvif.org/ver10/media/wsdl\"";
    // Its first JPEG profile.
    if (!ask(media, "<GetProfiles " + trt + "/>")) return false;
    const JPXmlElement* profiles = find(root, "GetProfilesResponse");
    std::string token;
    JPXmlElement config;
    if (profiles)
        for (const JPXmlElement& p : profiles->children) {
            const JPXmlElement* vec = childNamed(p, "VideoEncoderConfiguration");
            if (local(p.name) == "Profiles" && vec && textOf(childNamed(*vec, "Encoding")) == "JPEG") {
                token = p.attr("token");
                config = *vec;
                break;
            }
        }
    if (token.empty()) {
        why = "No JPEG profiles available for camera at " + m_host;
        return false;
    }
    // The JPEG resolutions it offers, and its ranges.
    if (!ask(media, "<GetVideoEncoderConfigurationOptions " + trt + "><ProfileToken>" + escape(token)
                        + "</ProfileToken></GetVideoEncoderConfigurationOptions>"))
        return false;
    const JPXmlElement* jpeg = nullptr;
    if (const JPXmlElement* options = find(root, "Options")) jpeg = childNamed(*options, "JPEG");
    m_resolutions.clear();
    if (jpeg)
        for (const JPXmlElement& r : jpeg->children)
            if (local(r.name) == "ResolutionsAvailable")
                m_resolutions.push_back({ std::atoi(textOf(childNamed(r, "Width")).c_str()),
                                          std::atoi(textOf(childNamed(r, "Height")).c_str()) });
    {
        std::lock_guard lk(s_knownLock);
        s_known[m_host] = m_resolutions;
    }
    // The largest, or the preferred one if it is offered.
    int chosen = -1;
    for (size_t i = 0; i < m_resolutions.size(); ++i)
        if (chosen < 0 || m_resolutions[i].width * m_resolutions[i].height
                              > m_resolutions[size_t(chosen)].width * m_resolutions[size_t(chosen)].height)
            chosen = int(i);
    for (size_t i = 0; i < m_resolutions.size(); ++i)
        if (!preferred.empty() && m_resolutions[i].text() == preferred) chosen = int(i);
    if (chosen >= 0) {
        // Set to it, at its best quality, fastest rate, shortest interval and highest bitrate.
        const JPXmlElement* opts = find(root, "Options");
        auto range = [](const JPXmlElement* parent, const char* name, const char* end) {
            const JPXmlElement* r = parent ? childNamed(*parent, name) : nullptr;
            return r ? textOf(childNamed(*r, end)) : std::string();
        };
        const std::string quality = range(opts, "QualityRange", "Max");
        const std::string rate = range(jpeg, "FrameRateRange", "Max");
        const std::string interval = range(jpeg, "EncodingIntervalRange", "Min");
        const JPXmlElement* ext = opts ? childNamed(*opts, "Extension") : nullptr;
        const std::string bitrate = range(ext ? childNamed(*ext, "JPEG") : nullptr, "BitrateRange", "Max");
        m_chosen = m_resolutions[size_t(chosen)];
        if (JPXmlElement* res = childNamed(config, "Resolution")) {
            setChild(*res, "Width", std::to_string(m_chosen.width));
            setChild(*res, "Height", std::to_string(m_chosen.height));
        }
        if (!quality.empty()) setChild(config, "Quality", quality);
        if (JPXmlElement* rc = childNamed(config, "RateControl")) {
            if (!rate.empty()) setChild(*rc, "FrameRateLimit", rate);
            if (!interval.empty()) setChild(*rc, "EncodingInterval", interval);
            if (!bitrate.empty()) setChild(*rc, "BitrateLimit", bitrate);
        }
        if (!ask(media, "<SetVideoEncoderConfiguration " + trt + ">" + write(config, "Configuration")
                            + "<ForcePersistence>true</ForcePersistence></SetVideoEncoderConfiguration>"))
            return false;
        JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "ONVIF camera " << m_host << ": " << m_chosen.text();
    }
    // Where its pictures are.
    if (!ask(media, "<GetSnapshotUri " + trt + "><ProfileToken>" + escape(token) + "</ProfileToken></GetSnapshotUri>"))
        return false;
    m_snapshotUri = textOf(find(root, "Uri"));
    if (m_snapshotUri.empty()) {
        why = m_host + ": it gave no snapshot address";
        return false;
    }
    JLOGC(JPlacerLog::kCamera, JLogLevel::Info) << "ONVIF camera " << m_host << ": snapshots from " << m_snapshotUri;
    return true;
}

std::vector<JPOnvif::Resolution> JPOnvif::knownResolutions(const std::string& host) {
    std::lock_guard lk(s_knownLock);
    const auto it = s_known.find(host);
    return it == s_known.end() ? std::vector<Resolution> {} : it->second;
}

} // inline namespace jf
