// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include <j/core/MenuSystem.h>

#include <functional>
#include <string>
#include <vector>

inline namespace jf {

// The keys: every function a key can be given, the key each has now, and the
// keys registered so that pressing one does its function (Edit >
// Preferences, Keys).
//
// A function has a default key (or none); a key chosen in Preferences is kept
// in the settings ("keys.<id>", JPlacerSettings::keyFor), "none" when its key
// was taken off. A key belongs to one function at a time: given to another,
// it is taken from the first. A function with a menu entry shows its key
// there, and its key does nothing while the entry is disabled.
//
// A key goes to the focused field first (JAppWindow's key routing), so a
// plain key (a digit, an arrow) still types or moves the caret in a text
// box; it acts only where nothing uses it.
class JPKeyMap {
public:
    struct Function {
        std::string           id;      // "jog.x+", "machine.home"
        std::string           group;   // as Preferences lists it: "Machine", "Jog"
        std::string           label;
        JMenuShortcut         byDefault;
        JMenuShortcut         key;     // now (Unknown: none)
        std::function<void()> run;
        JMenuItem*            item = nullptr;
    };

    JPKeyMap() = default;
    ~JPKeyMap();
    JPKeyMap(const JPKeyMap&)            = delete;
    JPKeyMap& operator=(const JPKeyMap&) = delete;

    // A function, with the key the settings give it (else its default),
    // registered. `item`: its menu entry, which shows the key; may be null.
    void add(const std::string& id, const std::string& group, const std::string& label, JMenuShortcut byDefault,
             std::function<void()> run, JMenuItem* item = nullptr);
    // Every function whose id starts with `prefix` taken away, its key
    // unregistered (the jog steps, made again when they change).
    void removeAll(const std::string& prefix);

    // Give `id` the key `key` (Unknown: none), kept in the settings. The
    // function that had the key loses it; its label is returned in
    // `takenFrom` ("" when none had it). False with `why` when the key cannot
    // be given (Tab, which moves between fields).
    bool assign(const std::string& id, JMenuShortcut key, std::string& takenFrom, std::string& why);
    // Back to its default (taken from whatever has it now), or every one back.
    void reset(const std::string& id);
    void resetAll();

    const std::vector<Function>& functions() const { return m_functions; }
    // The key `id` has now, as a menu shows it ("Ctrl+Right"); "" for none.
    std::string keyText(const std::string& id) const;
    // A key from what a keyboard sent (the Preferences key box); Unknown when
    // it carries no key.
    static JMenuShortcut fromEvent(const JKeyEvent& ke);

    // A key as it is kept ("Ctrl+Shift+P", "Ctrl+'", "F5") and read back.
    static std::string format(const JMenuShortcut& key);
    static JMenuShortcut parse(const std::string& text);

    // Called after any key changed: what shows keys (tooltips) shows them again.
    std::function<void()> onChanged;

private:
    Function* find(const std::string& id);
    void bind(Function& f, JMenuShortcut key);   // registered and shown, not kept
    void keep(const Function& f);                // into the settings
    static bool same(const JMenuShortcut& a, const JMenuShortcut& b);

    std::vector<Function> m_functions;
};

} // inline namespace jf
