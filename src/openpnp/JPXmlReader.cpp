// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPXmlReader.h"

#include <expat.h>

#include <fstream>
#include <iterator>

inline namespace jf {

namespace {

struct Builder {
    JPXmlElement               root;
    std::vector<JPXmlElement*> open;
};

void XMLCALL onStart(void* data, const XML_Char* name, const XML_Char** attrs) {
    auto* b = static_cast<Builder*>(data);
    JPXmlElement* e;
    if (b->open.empty()) {
        e = &b->root;
    } else {
        b->open.back()->children.emplace_back();
        e = &b->open.back()->children.back();
    }
    e->name = name;
    for (int i = 0; attrs[i]; i += 2) e->attributes[attrs[i]] = attrs[i + 1];
    b->open.push_back(e);
}

void XMLCALL onEnd(void* data, const XML_Char*) {
    static_cast<Builder*>(data)->open.pop_back();
}

void XMLCALL onText(void* data, const XML_Char* s, int len) {
    auto* b = static_cast<Builder*>(data);
    if (!b->open.empty()) b->open.back()->text.append(s, size_t(len));
}

} // namespace

bool JPXmlReader::read(const std::string& path, JPXmlElement& root, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = path + ": cannot be opened";
        return false;
    }
    const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!parse(src, root, error)) {
        error = path + ":" + error;
        return false;
    }
    return true;
}

bool JPXmlReader::parse(const std::string& src, JPXmlElement& root, std::string& error) {
    Builder b;
    XML_Parser p = XML_ParserCreate(nullptr);
    XML_SetUserData(p, &b);
    XML_SetElementHandler(p, onStart, onEnd);
    XML_SetCharacterDataHandler(p, onText);
    const bool ok = XML_Parse(p, src.data(), int(src.size()), XML_TRUE) == XML_STATUS_OK;
    if (!ok) error = std::to_string(XML_GetCurrentLineNumber(p)) + ": " + XML_ErrorString(XML_GetErrorCode(p));
    XML_ParserFree(p);
    if (ok) root = std::move(b.root);
    return ok;
}

} // inline namespace jf
