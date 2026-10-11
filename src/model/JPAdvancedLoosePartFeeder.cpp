// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPAdvancedLoosePartFeeder.h"

#include "openpnp/JPXmlJson.h"

inline namespace jf {

JPAdvancedLoosePartFeeder JPAdvancedLoosePartFeeder::fromXml(const JPXmlNode& own) {
    JPAdvancedLoosePartFeeder a;
    if (const JPXmlNode* p = own.child("pipeline")) a.pipeline = *p;
    if (const JPXmlNode* p = own.child("training-pipeline")) a.trainingPipeline = *p;
    return a;
}

JPAdvancedLoosePartFeeder JPAdvancedLoosePartFeeder::fromJson(const JJson& j) {
    JPAdvancedLoosePartFeeder a;
    if (j["pipeline"].isObject()) a.pipeline = JPXmlJson::node("pipeline", j["pipeline"]);
    if (j["trainingPipeline"].isObject()) a.trainingPipeline = JPXmlJson::node("training-pipeline", j["trainingPipeline"]);
    return a;
}

JJson JPAdvancedLoosePartFeeder::toJson() const {
    JJson j = JJson::object();
    if (pipeline) j["pipeline"] = JPXmlJson::from(*pipeline);
    if (trainingPipeline) j["trainingPipeline"] = JPXmlJson::from(*trainingPipeline);
    return j;
}

} // inline namespace jf
