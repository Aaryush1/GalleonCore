// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <galleon/Transport.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

// In-memory transport: records what the deck sends, replays queued reports.
// Tests keep a raw pointer to it after handing ownership to StreamDeck.
class FakeTransport final : public galleon::Transport
{
public:
    using Report = std::vector<std::uint8_t>;

    bool open() override
    {
        m_open = openSucceeds;
        return m_open;
    }
    void close() override { m_open = false; }
    [[nodiscard]] bool isOpen() const override { return m_open; }
    [[nodiscard]] QString serialNumber() const override { return QStringLiteral("FAKE0001"); }

    bool sendFeatureReport(std::span<const std::uint8_t> report) override
    {
        featureReports.emplace_back(report.begin(), report.end());
        return m_open;
    }

    bool write(std::span<const std::uint8_t> report) override
    {
        writes.emplace_back(report.begin(), report.end());
        return m_open;
    }

    int read(std::span<std::uint8_t> buffer) override
    {
        if (disconnected)
            return -1;
        if (pendingReads.empty())
            return 0;
        const Report report = pendingReads.front();
        pendingReads.pop_front();
        const std::size_t size = std::min(report.size(), buffer.size());
        std::copy_n(report.begin(), size, buffer.begin());
        return static_cast<int>(size);
    }

    bool openSucceeds = true;
    bool disconnected = false;
    std::vector<Report> featureReports;
    std::vector<Report> writes;
    std::deque<Report> pendingReads;

private:
    bool m_open = false;
};
