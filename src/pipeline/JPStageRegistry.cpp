// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPStageRegistry.h"

inline namespace jf {

JPStageRegistry::JPStageRegistry() {
    addParameterStages(m_types);
    addImageStages(m_types);
    addFilterStages(m_types);
    addDetectStages(m_types);
    addModelStages(m_types);
    addMatchStages(m_types);
    addTemplateStages(m_types);
    addDrawStages(m_types);
}

const JPStageRegistry& JPStageRegistry::instance() {
    static const JPStageRegistry registry;
    return registry;
}

const JPStageType* JPStageRegistry::find(const std::string& className) const {
    for (const JPStageType& t : m_types)
        if (t.className == className || t.typeName() == className) return &t;
    return nullptr;
}

} // inline namespace jf
