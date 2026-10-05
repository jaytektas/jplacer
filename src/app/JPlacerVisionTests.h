// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPlacerJob.h"
#include "JPlacerJobRun.h"
#include "JPlacerMachine.h"

#include "setup/JPVisionForms.h"

#include <functional>
#include <string>

inline namespace jf {

// The vision settings pages' tests on the machine, as OpenPnP's wizards
// run them: Test Alignment (the part on the chosen nozzle aligned over the
// camera looking up at the placement angle, then centred and turned to it
// when asked), Detect Offsets (centred by hand, aligned and centred, the
// difference kept as the settings' vision centre offsets) and Test
// Fiducial Locator (the fiducial found from where the head camera is, the
// camera moved onto it).
class JPlacerVisionTests {
public:
    JPlacerVisionTests(JPlacerJob& job, JPlacerMachine& machine, JPlacerJobRun& run);

    // What the pages' tests work with: the machine's test alignment angle, Center After Test.
    JPVisionForms::Tests tests();
    // `test`: "testAlignment", "detectOffsets" or "testFiducial"; `changed` told when settings changed.
    void run(const std::string& settingsId, const JPVisionForms::Holder& holder, const std::string& test,
             std::function<void()> changed);

private:
    // OpenPnP's getNozzleWithPart: the chosen nozzle, holding a part these
    // settings align; false and why when not.
    bool nozzleWithPart(const std::string& settingsId, const JPVisionForms::Holder& holder, std::string& nozzleId,
                        std::string& partId, std::string& why) const;
    void align(const std::string& settingsId, const JPVisionForms::Holder& holder, bool detectOffsets);
    void testFiducial(const JPVisionForms::Holder& holder);

    JPlacerJob&           m_job;
    JPlacerMachine&       m_machine;
    JPlacerJobRun&        m_run;
    bool                  m_center = true;
    std::function<void()> m_changed;
};

} // inline namespace jf
