// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "galleon/Protocol.h"

#include <algorithm>

namespace galleon::protocol {

std::vector<InputEvent> parseInputReport(std::span<const std::uint8_t> report)
{
    if (report.empty())
        return {};

    // TODO: decode key and dial reports, per node-elgato-stream-deck's
    // Galleon support. Until then every report is treated as unknown.
    return {};
}

namespace {

constexpr std::uint8_t KeepAliveCommand = 0x27;
constexpr std::uint8_t BrightnessCommand = 0x08;
constexpr std::uint8_t ScreenImageCommand = 0x0c;
constexpr std::uint8_t KeyImageCommand = 0x07;
constexpr std::size_t ScreenHeaderSize = 16;
constexpr std::size_t KeyHeaderSize = 8;

void putU16(std::vector<std::uint8_t> &out, std::size_t at, unsigned int value)
{
    out[at] = static_cast<std::uint8_t>(value & 0xFF);
    out[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

std::vector<std::uint8_t> featureReport(std::uint8_t command, std::uint8_t argument = 0)
{
    std::vector<std::uint8_t> report(FeatureReportSize, 0);
    report[0] = FeatureReportId;
    report[1] = command;
    report[2] = argument;
    return report;
}

} // namespace

std::vector<std::uint8_t> keepAliveReport()
{
    return featureReport(KeepAliveCommand);
}

std::vector<std::uint8_t> brightnessReport(int percent)
{
    if (percent < 0 || percent > 100)
        return {};
    return featureReport(BrightnessCommand, static_cast<std::uint8_t>(percent));
}

std::vector<std::vector<std::uint8_t>> screenImageReports(std::span<const std::uint8_t> jpeg,
                                                          int x, int y, int width, int height)
{
    if (jpeg.empty() || jpeg.size() > MaxJpegSize)
        return {};
    if (x < 0 || y < 0 || width <= 0 || height <= 0 || x > ScreenWidth - width || y > ScreenHeight - height)
        return {};

    constexpr std::size_t payloadMax = OutputReportSize - ScreenHeaderSize;
    std::vector<std::vector<std::uint8_t>> reports;
    reports.reserve(jpeg.size() / payloadMax + 1);
    for (std::size_t offset = 0, part = 0; offset < jpeg.size(); offset += payloadMax, ++part) {
        const std::size_t length = std::min(payloadMax, jpeg.size() - offset);
        std::vector<std::uint8_t> report(OutputReportSize, 0);
        report[0] = OutputReportId;
        report[1] = ScreenImageCommand;
        putU16(report, 2, static_cast<unsigned int>(x));
        putU16(report, 4, static_cast<unsigned int>(y));
        putU16(report, 6, static_cast<unsigned int>(width));
        putU16(report, 8, static_cast<unsigned int>(height));
        report[10] = offset + length >= jpeg.size() ? 1 : 0;
        putU16(report, 11, static_cast<unsigned int>(part));
        putU16(report, 13, static_cast<unsigned int>(length));
        report[15] = 0;
        std::copy_n(jpeg.begin() + static_cast<std::ptrdiff_t>(offset), length, report.begin() + ScreenHeaderSize);
        reports.push_back(std::move(report));
    }
    return reports;
}

std::vector<std::vector<std::uint8_t>> keyImageReports(int key, std::span<const std::uint8_t> jpeg)
{
    if (key < 0 || key >= KeyCount || jpeg.empty() || jpeg.size() > MaxJpegSize)
        return {};

    constexpr std::size_t payloadMax = OutputReportSize - KeyHeaderSize;
    std::vector<std::vector<std::uint8_t>> reports;
    reports.reserve(jpeg.size() / payloadMax + 1);
    for (std::size_t offset = 0, part = 0; offset < jpeg.size(); offset += payloadMax, ++part) {
        const std::size_t length = std::min(payloadMax, jpeg.size() - offset);
        std::vector<std::uint8_t> report(OutputReportSize, 0);
        report[0] = OutputReportId;
        report[1] = KeyImageCommand;
        report[2] = static_cast<std::uint8_t>(key);
        report[3] = offset + length >= jpeg.size() ? 1 : 0;
        putU16(report, 4, static_cast<unsigned int>(length));
        putU16(report, 6, static_cast<unsigned int>(part));
        std::copy_n(jpeg.begin() + static_cast<std::ptrdiff_t>(offset), length, report.begin() + KeyHeaderSize);
        reports.push_back(std::move(report));
    }
    return reports;
}

} // namespace galleon::protocol
