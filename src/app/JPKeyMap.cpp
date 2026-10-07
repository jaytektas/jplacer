// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPKeyMap.h"

#include "JPlacerSettings.h"

#include <j/config/Settings.h>

#include <algorithm>
#include <cctype>

inline namespace jf {

namespace {

// How a key taken off is kept: absent means "the default".
constexpr const char* kNone = "none";

using K = JKeyEvent::JKey;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A key's name, as JMenuShortcut::toString writes it, back to the key.
K keyNamed(const std::string& name) {
    if (name.empty()) return K::Unknown;
    const std::string n = lower(name);
    static const std::pair<const char*, K> named[] = {
        { "tab", K::Tab },     { "enter", K::Return }, { "space", K::Space },   { "esc", K::Escape },
        { "backspace", K::Backspace }, { "del", K::Delete }, { "left", K::Left }, { "right", K::Right },
        { "up", K::Up },       { "down", K::Down },    { "home", K::Home },     { "end", K::End },
        { "pageup", K::PageUp }, { "pagedown", K::PageDown },
    };
    for (const auto& [text, key] : named)
        if (n == text) return key;
    if (n.size() >= 2 && n[0] == 'f' && std::all_of(n.begin() + 1, n.end(), [](char c) { return std::isdigit(c); })) {
        const int f = std::stoi(n.substr(1));
        if (f >= 1 && f <= 12) return static_cast<K>(uint32_t(K::F1) + uint32_t(f - 1));
    }
    if (name.size() == 1) {
        const char c = char(std::toupper(static_cast<unsigned char>(name[0])));
        if (c >= 'A' && c <= 'Z') return static_cast<K>(uint32_t(K::A) + uint32_t(c - 'A'));
        if (c >= '0' && c <= '9') return static_cast<K>(uint32_t(K::_0) + uint32_t(c - '0'));
        if (c > 32 && c < 127) return static_cast<K>(uint32_t(static_cast<unsigned char>(c)));   // ' / , . = -
    }
    return K::Unknown;
}

} // namespace

JPKeyMap::~JPKeyMap() {
    for (const Function& f : m_functions)
        if (f.key.key != K::Unknown) JMenuManager::instance().unregisterShortcut(f.key);
}

bool JPKeyMap::same(const JMenuShortcut& a, const JMenuShortcut& b) {
    return a.key == b.key && a.ctrl == b.ctrl && a.alt == b.alt && a.shift == b.shift;
}

std::string JPKeyMap::format(const JMenuShortcut& key) {
    return key.toString();
}

JMenuShortcut JPKeyMap::parse(const std::string& text) {
    JMenuShortcut key;
    std::string rest = text;
    // The modifiers first, as toString writes them; what is left is the key
    // ("Ctrl++" is Ctrl and the plus key).
    for (bool more = true; more;) {
        more = false;
        for (const auto& [prefix, flag] : { std::pair<const char*, bool*>{ "ctrl+", &key.ctrl },
                                            std::pair<const char*, bool*>{ "alt+", &key.alt },
                                            std::pair<const char*, bool*>{ "shift+", &key.shift } }) {
            const std::string p = prefix;
            if (rest.size() > p.size() && lower(rest.substr(0, p.size())) == p) {
                *flag = true;
                rest  = rest.substr(p.size());
                more  = true;
            }
        }
    }
    key.key = keyNamed(rest);
    if (key.key == K::Unknown) return {};
    return key;
}

JMenuShortcut JPKeyMap::fromEvent(const JKeyEvent& ke) {
    JMenuShortcut key{ ke.key, ke.ctrl, ke.alt, ke.shift };
    // A key the platform names only by its character (punctuation).
    if (key.key == K::Unknown && ke.utf8[0] != '\0' && ke.utf8[1] == '\0')
        key.key = keyNamed(std::string(1, ke.utf8[0]));
    if (key.key == K::Unknown) return {};
    return key;
}

JPKeyMap::Function* JPKeyMap::find(const std::string& id) {
    for (Function& f : m_functions)
        if (f.id == id) return &f;
    return nullptr;
}

void JPKeyMap::add(const std::string& id, const std::string& group, const std::string& label,
                   JMenuShortcut byDefault, std::function<void()> run, JMenuItem* item) {
    Function f;
    f.id        = id;
    f.group     = group;
    f.label     = label;
    f.byDefault = byDefault;
    f.run       = std::move(run);
    f.item      = item;
    m_functions.push_back(std::move(f));
    // The key kept for it, else its default, unless another function has it.
    const std::string kept = JSettings::instance().get<std::string>(JPlacerSettings::keyFor(id), "");
    JMenuShortcut key = kept.empty() ? byDefault : kept == kNone ? JMenuShortcut{} : parse(kept);
    for (const Function& other : m_functions)
        if (other.id != id && key.key != K::Unknown && same(other.key, key)) key = {};
    bind(m_functions.back(), key);
}

void JPKeyMap::removeAll(const std::string& prefix) {
    std::erase_if(m_functions, [&](const Function& f) {
        if (f.id.rfind(prefix, 0) != 0) return false;
        if (f.key.key != K::Unknown) JMenuManager::instance().unregisterShortcut(f.key);
        return true;
    });
}

void JPKeyMap::bind(Function& f, JMenuShortcut key) {
    if (f.key.key != K::Unknown) JMenuManager::instance().unregisterShortcut(f.key);
    f.key = key;
    if (f.item) f.item->setShortcut(key);
    if (key.key == K::Unknown) return;
    JMenuManager::instance().registerShortcut(key, [this, id = f.id] {
        Function* g = find(id);
        if (!g || (g->item && !g->item->isEnabled())) return;
        g->run();
    });
}

void JPKeyMap::keep(const Function& f) {
    const std::string name = JPlacerSettings::keyFor(f.id);
    if (same(f.key, f.byDefault)) JPlacerSettings::remove(name);
    else JSettings::instance().set(name, f.key.key == K::Unknown ? std::string(kNone) : format(f.key));
}

bool JPKeyMap::assign(const std::string& id, JMenuShortcut key, std::string& takenFrom, std::string& why) {
    takenFrom.clear();
    Function* f = find(id);
    if (!f) {
        why = "there is no such function";
        return false;
    }
    if (key.key == K::Tab || key.key == K::BackTab) {
        why = "Tab moves between fields";
        return false;
    }
    if (key.key != K::Unknown)
        for (Function& other : m_functions)
            if (other.id != id && same(other.key, key)) {
                takenFrom = other.label;
                bind(other, {});
                keep(other);
            }
    bind(*f, key);
    keep(*f);
    JPlacerSettings::save();
    if (onChanged) onChanged();
    return true;
}

void JPKeyMap::reset(const std::string& id) {
    Function* f = find(id);
    if (!f) return;
    std::string takenFrom, why;
    assign(id, f->byDefault, takenFrom, why);
}

void JPKeyMap::resetAll() {
    // Every key off first, so no default is refused for one not yet put back.
    for (Function& f : m_functions) bind(f, {});
    for (Function& f : m_functions) {
        bool free = true;
        for (const Function& other : m_functions)
            if (&other != &f && f.byDefault.key != K::Unknown && same(other.key, f.byDefault)) free = false;
        bind(f, free ? f.byDefault : JMenuShortcut{});
        JPlacerSettings::remove(JPlacerSettings::keyFor(f.id));
    }
    JPlacerSettings::save();
    if (onChanged) onChanged();
}

std::string JPKeyMap::keyText(const std::string& id) const {
    for (const Function& f : m_functions)
        if (f.id == id) return format(f.key);
    return "";
}

} // inline namespace jf
