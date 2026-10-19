// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "galleon/Protocol.h"

#include <algorithm>

namespace galleon::protocol {

namespace {

constexpr std::uint8_t InputReportId = 0x01;
constexpr std::uint8_t InputKindKeys = 0x00;
constexpr std::uint8_t InputKindDials = 0x03;
constexpr std::uint8_t DialSubButtons = 0x00;
constexpr std::uint8_t DialSubRotation = 0x01;
constexpr std::size_t InputHeaderSize = 4;

InputResult invalid()
{
    return {InputStatus::Invalid, {}};
}

unsigned int u16le(std::span<const std::uint8_t> report, std::size_t at)
{
    return static_cast<unsigned int>(report[at]) | (static_cast<unsigned int>(report[at + 1]) << 8);
}

InputResult decodeKeys(std::span<const std::uint8_t> report, InputState &state)
{
    if (u16le(report, 2) != static_cast<unsigned int>(KeyCount) || report.size() < InputHeaderSize + KeyCount)
        return invalid();
    std::array<bool, KeyCount> pressed{};
    for (int i = 0; i < KeyCount; ++i) {
        const std::uint8_t byte = report[InputHeaderSize + static_cast<std::size_t>(i)];
        if (byte > 1)
            return invalid();
        pressed[static_cast<std::size_t>(i)] = byte == 1;
    }

    InputResult result{InputStatus::Ok, {}};
    for (int i = 0; i < KeyCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        if (pressed[index] == state.keys[index])
            continue;
        result.events.push_back({pressed[index] ? InputEvent::Type::KeyPressed : InputEvent::Type::KeyReleased, i, 0});
    }
    state.keys = pressed;
    return result;
}

InputResult decodeDials(std::span<const std::uint8_t> report, InputState &state)
{
    constexpr std::size_t countField = 1 + DialCount; // the kind byte, then one byte per dial
    if (u16le(report, 2) != countField || report.size() < InputHeaderSize + countField)
        return invalid();
    const std::uint8_t kind = report[InputHeaderSize];
    const auto data = report.subspan(InputHeaderSize + 1, DialCount);

    InputResult result{InputStatus::Ok, {}};
    if (kind == DialSubButtons) {
        std::array<bool, DialCount> pressed{};
        for (std::size_t i = 0; i < DialCount; ++i) {
            if (data[i] > 1)
                return invalid();
            pressed[i] = data[i] == 1;
        }
        for (std::size_t i = 0; i < DialCount; ++i) {
            if (pressed[i] == state.dialButtons[i])
                continue;
            result.events.push_back(
                {pressed[i] ? InputEvent::Type::DialPressed : InputEvent::Type::DialReleased, static_cast<int>(i), 0});
        }
        state.dialButtons = pressed;
        return result;
    }
    if (kind == DialSubRotation) {
        std::array<int, DialCount> steps{};
        for (std::size_t i = 0; i < DialCount; ++i) {
            steps[i] = static_cast<std::int8_t>(data[i]);
            if (steps[i] > MaxDialSteps || steps[i] < -MaxDialSteps)
                return invalid();
        }
        for (std::size_t i = 0; i < DialCount; ++i) {
            if (steps[i] != 0)
                result.events.push_back({InputEvent::Type::DialRotated, static_cast<int>(i), steps[i]});
        }
        return result;
    }
    return {InputStatus::Ignored, {}};
}

} // namespace

InputResult decodeInputReport(std::span<const std::uint8_t> report, InputState &state)
{
    if (report.empty() || report[0] != InputReportId)
        return {InputStatus::Ignored, {}};
    if (report.size() < InputHeaderSize)
        return invalid();
    switch (report[1]) {
    case InputKindKeys:
        return decodeKeys(report, state);
    case InputKindDials:
        return decodeDials(report, state);
    default:
        return {InputStatus::Ignored, {}};
    }
}

std::vector<InputEvent> parseInputReport(std::span<const std::uint8_t> report)
{
    InputState state;
    return decodeInputReport(report, state).events;
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
