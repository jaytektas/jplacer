// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// Every kind of feeder's page: a new feeder of each of OpenPnP's classes is of its typed kind, its page is made,
// and every field on it reads and takes back what it reads (each field reaching its kind's own settings); and the
// feeder comes back through jplacer's file as its kind.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>

#include "setup/JPFeederForms.h"

#include <cstdio>
#include <filesystem>

using namespace jf;
namespace fs = std::filesystem;

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-feeder-forms";
    fs::remove_all(dir);
    fs::create_directories(dir);
    JPConfiguration config(dir.string());
    std::vector<std::string> problems;
    std::string error;
    assert(config.load(problems, error));
    for (const std::string& className : JPFeeder::classNames()) {
        JPFeeder made = JPFeeder::create(className, "");
        assert(made.typed() || (std::fprintf(stderr, "%s is not typed\n", className.c_str()), false));
        const std::string id = made.id();
        config.addFeeder(std::move(made));
        JPFeederForms::Options options;
        std::map<std::string, bool> clone;
        options.cloneChoices = &clone;
        const JPSetupProperties::Form form = JPFeederForms::forFeeder(config, id, nullptr, options);
        assert(!form.model.all().empty() || (std::fprintf(stderr, "%s has no page\n", className.c_str()), false));
        for (const JProperty& p : form.model.all()) {
            const JVariant v = p.get();
            if (p.writable()) p.set(v);
        }
        const JPFeeder back = JPFeeder::fromJson(JJson::parse(config.feeder(id)->toJson().dump()));
        assert(back.typed() && back.className() == className);
    }
    fs::remove_all(dir);
    return 0;
}
