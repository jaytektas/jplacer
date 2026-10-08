// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

// The library's file (DESIGN.md, Storage): made from OpenPnP's parts.xml and packages.xml the first time (they
// are left as they were); its parts' and packages' ids for good (UUIDs), values, identifiers and AKAs kept;
// OpenPnP's files changed later bring in only what the library lacks (what it has is not overwritten); a
// library of a newer jplacer is refused, not read wrongly.
// Tests check with assert(); a Release build must not compile it away.
#undef NDEBUG
#include <cassert>
#include <cmath>

#include "model/JPConfiguration.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace jf;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& p, const std::string& text) { std::ofstream(p) << text; }

std::string read(const fs::path& p) {
    std::ifstream f(p);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "jplacer-test-library-store";
    fs::remove_all(dir);
    fs::create_directories(dir);
    write(dir / "packages.xml", "<openpnp-packages><package id=\"C0603\"/></openpnp-packages>\n");
    write(dir / "parts.xml", "<openpnp-parts><part id=\"C0603-100n\" package-id=\"C0603\" height=\"0.8\" "
                             "height-units=\"Millimeters\"/></openpnp-parts>\n");
    const std::string partsXml = read(dir / "parts.xml");
    std::string uuid;
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        std::string error;
        assert(c.load(problems, error) && problems.empty());
        assert(fs::exists(dir / JPLibraryStore::kFile) && c.parts().size() == 1 && c.packages().size() == 1);
        JPPart* p = c.part("C0603-100n");
        assert(p && p->uuid.size() == 36 && c.package("C0603")->uuid.size() == 36 && p->packageId == "C0603");
        uuid = p->uuid;
        p->value = "100n";
        p->name = "Capacitor, X7R";
        p->datasheet = "https://example.com/cl10.pdf";
        p->identifiers.push_back({ "mpn", "Samsung", "CL10B104KB8NNNC" });
        p->identifiers.push_back({ "supplierPn", "LCSC", "C14663" });
        p->akas.push_back({ "valueFootprint", "100n|C_0603_1608Metric", "Ctrl.jpboard", "2026-10-08T12:00:00" });
        c.defaultFootprint("C0603", true)->cadNames.push_back("C_0603_1608Metric");
        p->packagings.push_back({ "Reel", 8, 4, "Paper", 90, 4000, "7 inch" });
        p->offers.push_back({ "LCSC", "C14663", "Reel", 4000, "1: 0.0016, 4000: 0.0011", "https://lcsc.com/C14663", "0.0011", "2026-10-08" });
        c.manufacturers().push_back({ "Texas Instruments", { "TI", "Texas Instruments Inc." } });
        assert(c.save(error));
        assert(read(dir / "parts.xml") == partsXml);   // OpenPnP's file left as it was
    }
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        std::string error;
        assert(c.load(problems, error) && problems.empty());
        const JPPart* p = c.part("C0603-100n");
        assert(p && p->uuid == uuid && p->value == "100n" && p->name == "Capacitor, X7R" && p->identifiers.size() == 2);
        assert(p->identifiers[0].org == "Samsung" && p->identifiers[1].code == "C14663");
        assert(p->akas.size() == 1 && p->akas[0].text == "100n|C_0603_1608Metric" && p->akas[0].learnedFrom == "Ctrl.jpboard");
        assert(c.footprints().size() == 1 && c.footprints()[0]->cadNames.size() == 1 && c.footprints()[0]->packageId == "C0603");
        assert(c.packageNamed("c_0603_1608metric") == c.package("C0603") && std::abs(p->height.value() - 0.8) < 1e-9);
        assert(p->packagings.size() == 1 && p->packagings[0].kind == "Reel" && p->packagings[0].rotationDeg == 90
               && p->packagings[0].quantity == 4000 && p->packagings[0].note == "7 inch");
        assert(p->offers.size() == 1 && p->offers[0].sku == "C14663" && p->offers[0].moq == 4000 && p->offers[0].lastPrice == "0.0011");
        assert(c.manufacturers().size() == 1 && c.manufacturers()[0].akas.size() == 2 && c.manufacturerName("ti") == "Texas Instruments");
    }
    // OpenPnP's parts.xml changed (copied in again): what the library lacks is added; what it has is kept.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    write(dir / "parts.xml", "<openpnp-parts><part id=\"C0603-100n\" package-id=\"C0805\"/>"
                             "<part id=\"R0603-10k\" package-id=\"C0603\"/></openpnp-parts>\n");
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        std::string error;
        assert(c.load(problems, error) && problems.size() == 1 && c.parts().size() == 2);
        assert(c.part("C0603-100n")->packageId == "C0603" && c.part("C0603-100n")->value == "100n" && c.part("R0603-10k"));
    }
    {
        // Not looked at again while unchanged.
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        std::string error;
        assert(c.load(problems, error) && problems.empty() && c.parts().size() == 2);
    }
    // A library of a newer jplacer.
    {
        JPLibraryStore s;
        std::string error;
        assert(s.open((dir / JPLibraryStore::kFile).string(), error) && s.setMeta("schema", "99"));
    }
    {
        JPConfiguration c(dir.string());
        std::vector<std::string> problems;
        std::string error;
        assert(!c.load(problems, error) && error.find("newer jplacer") != std::string::npos);
    }
    fs::remove_all(dir);
    return 0;
}
