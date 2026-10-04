// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPFeedersTableModel.h"

inline namespace jf {

namespace {

enum Col { kName, kPart, kType, kPriority, kFaults, kEnabled, kFeed, kColumns };

constexpr JPFeeder::Priority kPriorities[] = { JPFeeder::Priority::High, JPFeeder::Priority::Normal, JPFeeder::Priority::Low };
constexpr JPFeeder::FeedOptions kFeedOptions[] = { JPFeeder::FeedOptions::Normal, JPFeeder::FeedOptions::SkipNext,
                                                   JPFeeder::FeedOptions::Disable };

} // namespace

JPFeedersTableModel::JPFeedersTableModel(JPConfiguration& config) : m_config(config) {}

int JPFeedersTableModel::columnCount() const { return kColumns; }

JPTableModel::Column JPFeedersTableModel::column(int c) const {
    switch (c) {
        case kName:     return { "Name", "", Kind::Text };
        case kPart:     return { "Part", "", Kind::Text };
        case kType:     return { "Type", "", Kind::Text };
        case kPriority: return { "Priority", "", Kind::Choice };
        case kFaults:   return { "Faults", "", Kind::Text };
        case kEnabled:  return { "Enabled", "", Kind::Boolean };
        case kFeed:     return { "Feed", "", Kind::Choice };
    }
    return {};
}

int JPFeedersTableModel::rowCount() const { return int(m_config.feeders().size()); }

JPFeeder* JPFeedersTableModel::feeder(int row) const {
    auto& f = m_config.feeders();
    return row >= 0 && size_t(row) < f.size() ? &f[size_t(row)] : nullptr;
}

int JPFeedersTableModel::rowOf(const std::string& feederId) const {
    const auto& f = m_config.feeders();
    for (size_t i = 0; i < f.size(); ++i)
        if (f[i].id() == feederId) return int(i);
    return -1;
}

std::string JPFeedersTableModel::rowKey(int row) const {
    const JPFeeder* f = feeder(row);
    return f ? f->id() : std::string();
}

std::string JPFeedersTableModel::text(int row, int c) const {
    const JPFeeder* f = feeder(row);
    if (!f) return {};
    switch (c) {
        case kName:     return f->name();
        case kPart:     return f->partId();
        case kType:     return f->typeName();
        case kPriority: return JPFeeder::priorityName(f->priority());
        case kFaults:   return f->summariseJobFaults();
        case kFeed:     return JPFeeder::feedOptionsName(f->feedOptions());
    }
    return {};
}

bool JPFeedersTableModel::checked(int row, int c) const {
    const JPFeeder* f = feeder(row);
    return f && c == kEnabled && f->enabled();
}

std::optional<int> JPFeedersTableModel::compare(int a, int b, int c) const {
    const JPFeeder *fa = feeder(a), *fb = feeder(b);
    if (!fa || !fb) return std::nullopt;
    if (c == kPriority) return int(fa->priority()) - int(fb->priority());
    if (c == kFeed) return int(fa->feedOptions()) - int(fb->feedOptions());
    return std::nullopt;
}

bool JPFeedersTableModel::cellDimmed(int row, int c) const {
    const JPFeeder* f = feeder(row);
    return f && c == kEnabled && partUsed && !partUsed(f->partId());
}

bool JPFeedersTableModel::editable(int row, int c) const {
    const JPFeeder* f = feeder(row);
    if (!f) return false;
    return c == kName || c == kEnabled || c == kPriority || (c == kFeed && f->supportsFeedOptions());
}

std::vector<std::string> JPFeedersTableModel::choices(int, int c) const {
    std::vector<std::string> out;
    if (c == kPriority)
        for (const auto p : kPriorities) out.push_back(JPFeeder::priorityName(p));
    if (c == kFeed)
        for (const auto o : kFeedOptions) out.push_back(JPFeeder::feedOptionsName(o));
    return out;
}

bool JPFeedersTableModel::setText(int row, int c, const std::string& text, std::string&) {
    JPFeeder* f = feeder(row);
    if (!f || c != kName) return false;
    f->setName(text);
    if (onChanged) onChanged();
    return true;
}

void JPFeedersTableModel::setChoice(int row, int c, int index) {
    JPFeeder* f = feeder(row);
    if (!f || index < 0 || index > 2) return;
    if (c == kPriority) f->setPriority(kPriorities[index]);
    else if (c == kFeed && f->supportsFeedOptions()) f->setFeedOptions(kFeedOptions[index]);
    else return;
    if (onChanged) onChanged();
}

void JPFeedersTableModel::setChecked(int row, int c, bool on) {
    JPFeeder* f = feeder(row);
    if (!f || c != kEnabled) return;
    f->setEnabled(on);
    if (onChanged) onChanged();
}

} // inline namespace jf
