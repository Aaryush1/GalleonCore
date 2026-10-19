// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PagesConfig.h>

#include <QByteArray>

#include <cstddef>
#include <cstdint>

// The pages file is user-written external data: no input may crash the parser,
// and a result is either a config or errors, never both nor neither.
// Run: ./fuzz_pages_config -max_total_time=60
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    const QByteArray bytes(reinterpret_cast<const char *>(data), static_cast<qsizetype>(size));
    const galleond::PagesParseResult result = galleond::parsePagesConfig(bytes);
    if (result.config.has_value() != result.errors.isEmpty())
        __builtin_trap();
    return 0;
}
