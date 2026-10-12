// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPBoardPhoto.h"

#include "common/JPWholeFile.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

inline namespace jf {

namespace fs = std::filesystem;

namespace {
constexpr const char* kKindKeys[] = { "jobStart", "jobEnd", "asked" };
constexpr const char* kSeparator = " \xC2\xB7 ";   // " · "
}

const char* JPBoardPhoto::kindName(Kind k) {
    switch (k) {
        case Kind::JobStart: return "Job start";
        case Kind::JobEnd:   return "Job end";
        case Kind::Asked:    return "Asked for";
    }
    return "";
}

std::string JPBoardPhoto::label() const {
    std::string s = taken + kSeparator + kindName(kind);
    if (!boardLocation.empty()) s += kSeparator + boardLocation;
    return s;
}

std::string JPBoardPhoto::folderOf(const std::string& boardFile) {
    return boardFile.empty() ? std::string() : boardFile + kFolderSuffix;
}

std::string JPBoardPhoto::fileIn(const std::string& folder) const {
    // Named so it sorts by when it was taken: a file name takes no colons.
    std::string name = taken;
    std::replace(name.begin(), name.end(), ':', '-');
    if (!boardLocation.empty()) name += " " + boardLocation;
    name += std::string(" ") + kKindKeys[int(kind)];
    for (char& c : name)
        if (c == '/' || c == '\\') c = '-';
    return (fs::path(folder) / (name + ".png")).string();
}

std::string JPBoardPhoto::recordOf(const std::string& png) {
    return (fs::path(png).replace_extension(".json")).string();
}

std::vector<JPBoardPhoto> JPBoardPhoto::of(const std::string& boardFile) {
    std::vector<JPBoardPhoto> out;
    const std::string folder = folderOf(boardFile);
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) return out;
    for (const fs::directory_entry& e : fs::directory_iterator(folder, ec)) {
        if (e.path().extension() != ".png") continue;
        const std::string record = recordOf(e.path().string());
        if (!fs::exists(record, ec)) continue;
        out.push_back(fromJson(JJson::parseFile(record), e.path().string()));
    }
    std::sort(out.begin(), out.end(), [](const JPBoardPhoto& a, const JPBoardPhoto& b) {
        return a.taken != b.taken ? a.taken > b.taken : a.file > b.file;
    });
    return out;
}

bool JPBoardPhoto::saveRecord(std::string& why) const {
    return JPWholeFile::write(recordOf(file), toJson(), why);
}

bool JPBoardPhoto::remove(std::string& why) const {
    std::error_code ec;
    for (const std::string& f : { file, recordOf(file) }) {
        fs::remove(f, ec);
        if (ec) {
            why = f + ": " + ec.message();
            return false;
        }
    }
    // The last of them: the folder too.
    const fs::path folder = fs::path(file).parent_path();
    if (fs::is_empty(folder, ec)) fs::remove(folder, ec);
    return true;
}

JJson JPBoardPhoto::toJson() const {
    JJson j = JJson::object();
    j["kind"] = kKindKeys[int(kind)];
    j["taken"] = taken;
    j["job"] = job;
    j["run"] = run;
    j["boardLocation"] = boardLocation;
    j["side"] = side == JPSide::Bottom ? "Bottom" : "Top";
    j["mmPerPixel"] = mmPerPixel;
    j["originX"] = originX;
    j["originY"] = originY;
    j["width"] = width;
    j["height"] = height;
    JJson n = JJson::array();
    for (const std::string& s : notes) n.push(JJson(s));
    j["notes"] = n;
    return j;
}

JPBoardPhoto JPBoardPhoto::fromJson(const JJson& j, const std::string& file) {
    JPBoardPhoto p;
    p.file = file;
    if (!j.isObject()) return p;
    for (int k = 0; k < 3; ++k)
        if (j["kind"].str() == kKindKeys[k]) p.kind = Kind(k);
    p.taken = j["taken"].str();
    p.job = j["job"].str();
    p.run = j["run"].str();
    p.boardLocation = j["boardLocation"].str();
    p.side = j["side"].str() == "Bottom" ? JPSide::Bottom : JPSide::Top;
    p.mmPerPixel = j["mmPerPixel"].isNumber() ? j["mmPerPixel"].number() : 0;
    p.originX = j["originX"].isNumber() ? j["originX"].number() : 0;
    p.originY = j["originY"].isNumber() ? j["originY"].number() : 0;
    p.width = j["width"].isNumber() ? int(j["width"].number()) : 0;
    p.height = j["height"].isNumber() ? int(j["height"].number()) : 0;
    for (size_t i = 0; i < j["notes"].size(); ++i) p.notes.push_back(j["notes"][i].str());
    return p;
}

} // inline namespace jf
