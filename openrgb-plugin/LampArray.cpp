// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LampArray.h"

namespace galleon::openrgb {

namespace {

constexpr std::uint8_t ItemReportId = 0x85;
constexpr std::uint8_t ItemUsage = 0x09;
constexpr std::uint8_t ItemCollection = 0xA1;
constexpr std::uint8_t CollectionLogical = 0x02;

constexpr std::uint8_t UsageAttributes = 0x02;
constexpr std::uint8_t UsageRequest = 0x20;
constexpr std::uint8_t UsageResponse = 0x22;

// A lamp array larger than 10 m is not a keyboard: reject the report.
constexpr std::uint32_t MaxDimensionUm = 10'000'000;

std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t at)
{
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
           | (static_cast<std::uint32_t>(bytes[at + 2]) << 16) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

} // namespace

std::optional<ReportIds> findReportIds(std::span<const std::uint8_t> descriptor)
{
    if (descriptor.empty() || descriptor.size() > MaxDescriptorSize)
        return std::nullopt;

    ReportIds ids;
    // Each match is six bytes: 85 id  09 usage  a1 02.
    for (std::size_t i = 0; i + 5 < descriptor.size(); ++i) {
        if (descriptor[i] != ItemReportId || descriptor[i + 2] != ItemUsage || descriptor[i + 4] != ItemCollection
            || descriptor[i + 5] != CollectionLogical)
            continue;

        const std::uint8_t id = descriptor[i + 1];
        std::uint8_t *target = nullptr;
        switch (descriptor[i + 3]) {
        case UsageAttributes:
            target = &ids.attributes;
            break;
        case UsageRequest:
            target = &ids.request;
            break;
        case UsageResponse:
            target = &ids.response;
            break;
        default:
            break;
        }
        if (!target)
            continue;
        if (id == 0 || *target != 0)
            return std::nullopt; // report ID 0 means "no ID"; a second match is ambiguous
        *target = id;
    }

    if (ids.attributes == 0 || ids.request == 0 || ids.response == 0)
        return std::nullopt;
    return ids;
}

std::optional<Attributes> parseAttributes(std::span<const std::uint8_t> report)
{
    if (report.size() != AttributesReportSize)
        return std::nullopt;

    Attributes attributes;
    attributes.lampCount = readU16(report, 1);
    attributes.widthUm = readU32(report, 3);
    attributes.heightUm = readU32(report, 7);
    attributes.depthUm = readU32(report, 11);
    attributes.kind = readU32(report, 15);
    attributes.minUpdateIntervalUs = readU32(report, 19);

    if (attributes.lampCount == 0 || attributes.lampCount > MaxLamps)
        return std::nullopt;
    if (attributes.widthUm > MaxDimensionUm || attributes.heightUm > MaxDimensionUm
        || attributes.depthUm > MaxDimensionUm)
        return std::nullopt;
    return attributes;
}

std::array<std::uint8_t, LampRequestReportSize> makeLampRequest(std::uint8_t requestId, unsigned int lampId)
{
    return {requestId, static_cast<std::uint8_t>(lampId & 0xFF), static_cast<std::uint8_t>((lampId >> 8) & 0xFF)};
}

std::optional<Lamp> parseLampResponse(std::span<const std::uint8_t> report, unsigned int expectedId)
{
    if (report.size() != LampResponseReportSize)
        return std::nullopt;

    Lamp lamp;
    lamp.id = readU16(report, 1);
    if (lamp.id != expectedId)
        return std::nullopt;

    lamp.xUm = readU32(report, 3);
    lamp.yUm = readU32(report, 7);
    lamp.zUm = readU32(report, 11);
    lamp.latencyUs = readU32(report, 15);
    lamp.purposes = readU32(report, 19);
    lamp.redLevels = report[23];
    lamp.greenLevels = report[24];
    lamp.blueLevels = report[25];
    lamp.intensityLevels = report[26];
    lamp.programmable = report[27] != 0;
    lamp.inputBinding = report[28];
    return lamp;
}

} // namespace galleon::openrgb
