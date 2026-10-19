// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <cstdlib>
#include <optional>

// The value of an optional that a test needs to be there. A test that reads a
// missing value stops at once (the check was meant to be a failure, not a
// crash further on), and static analysis can see that the access is guarded.
template <typename T>
const T &required(const std::optional<T> &value)
{
    if (!value) {
        qCritical("a test needed a value that is not there");
        std::abort();
    }
    return *value;
}
