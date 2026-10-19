// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <span>

// `count` bytes of `bytes` from `offset`, as a Qt byte array. A copy: no cast of
// the buffer's pointer and no pointer arithmetic. The caller has checked the sizes.
inline QByteArray bytesOf(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count)
{
    QByteArray out;
    out.reserve(static_cast<qsizetype>(count));
    for (const std::uint8_t byte : bytes.subspan(offset, count))
        out.append(static_cast<char>(byte));
    return out;
}
