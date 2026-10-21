// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <galleon/Transport.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

// In-memory transport: records what the deck sends, replays queued reports.
// Tests keep a raw pointer to it after handing ownership to StreamDeck.
class FakeTransport final : public galleon::Transport
{
public:
    using Report = std::vector<std::uint8_t>;

    bool open() override
    {
        ++openAttempts;
        m_open = openSucceeds && openAttempts != failOpenAttempt;
        return m_open;
    }
    void close() override
    {
        ++closeCalls;
        m_open = false;
    }
    [[nodiscard]] bool isOpen() const override { return m_open; }
    [[nodiscard]] QString serialNumber() const override { return QStringLiteral("FAKE0001"); }

    bool sendFeatureReport(std::span<const std::uint8_t> report) override
    {
        featureReports.emplace_back(report.begin(), report.end());
        return m_open && featureSucceeds && static_cast<int>(featureReports.size()) != failFeatureCall;
    }

    bool write(std::span<const std::uint8_t> report) override
    {
        if (beforeWrite)
            beforeWrite();
        writes.emplace_back(report.begin(), report.end());
        return m_open && writeSucceeds;
    }

    int read(std::span<std::uint8_t> buffer) override
    {
        if (disconnected)
            return -1;
        if (openAttempts < inputReadyOnOpenAttempt)
            return 0;
        if (pendingReads.empty())
            return 0;
        const Report report = pendingReads.front();
        pendingReads.pop_front();
        const std::size_t size = std::min(report.size(), buffer.size());
        std::copy_n(report.begin(), size, buffer.begin());
        return static_cast<int>(size);
    }

    std::function<void()> beforeWrite;
    int openAttempts = 0;
    int closeCalls = 0;
    int failOpenAttempt = 0;
    int failFeatureCall = 0;
    int inputReadyOnOpenAttempt = 1;
    bool featureSucceeds = true;
    bool writeSucceeds = true;
    bool openSucceeds = true;
    bool disconnected = false;
    std::vector<Report> featureReports;
    std::vector<Report> writes;
    std::deque<Report> pendingReads;

private:
    bool m_open = false;
};
