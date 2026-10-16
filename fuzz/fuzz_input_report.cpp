// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <galleon/Protocol.h>

#include <cstddef>
#include <cstdint>

// Run: ./fuzz_input_report -max_total_time=60
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    galleon::protocol::parseInputReport({data, size});
    return 0;
}
