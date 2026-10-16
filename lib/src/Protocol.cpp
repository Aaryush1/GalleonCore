// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "galleon/Protocol.h"

namespace galleon::protocol {

std::vector<InputEvent> parseInputReport(std::span<const std::uint8_t> report)
{
    if (report.empty())
        return {};

    // TODO: decode key and dial reports, per node-elgato-stream-deck's
    // Galleon support. Until then every report is treated as unknown.
    return {};
}

std::vector<std::uint8_t> keepAliveReport()
{
    // TODO: bytes from galleon-deck docs/PROTOCOL.md, once confirmed.
    return {};
}

} // namespace galleon::protocol
