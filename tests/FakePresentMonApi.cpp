// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../third_party/PresentMonAPI.h"
#include <QtCore/qglobal.h>
#include <QByteArray>
#include <cstring>
#include <limits>
#include <span>
struct PM_SESSION {};
struct PM_DYNAMIC_QUERY {};
namespace {
PM_SESSION &sessionToken() { static PM_SESSION token; return token; }
PM_DYNAMIC_QUERY &queryToken() { static PM_DYNAMIC_QUERY token; return token; }
QByteArray mode() { return qgetenv("GALLEON_PM_TEST_MODE"); }
}
extern "C" {
PRESENTMON_API2_EXPORT PM_STATUS pmGetApiVersion(PM_VERSION *version) {
    *version = {}; version->major = uint16_t(mode() == "version" ? 99 : PM_API_VERSION_MAJOR); return PM_STATUS_SUCCESS;
}
PRESENTMON_API2_EXPORT PM_STATUS pmOpenSession(PM_SESSION_HANDLE *handle) {
    if (mode() == "offline") return PM_STATUS_PIPE_ERROR;
    *handle = &sessionToken(); return PM_STATUS_SUCCESS;
}
PRESENTMON_API2_EXPORT PM_STATUS pmCloseSession(PM_SESSION_HANDLE /*session*/) { return PM_STATUS_SUCCESS; }
PRESENTMON_API2_EXPORT PM_STATUS pmStartTrackingProcess(PM_SESSION_HANDLE /*session*/, uint32_t process_id) { return process_id ? PM_STATUS_SUCCESS : PM_STATUS_INVALID_PID; }
PRESENTMON_API2_EXPORT PM_STATUS pmStopTrackingProcess(PM_SESSION_HANDLE /*session*/, uint32_t /*pid*/) { return PM_STATUS_SUCCESS; }
PRESENTMON_API2_EXPORT PM_STATUS pmRegisterDynamicQuery(PM_SESSION_HANDLE /*session*/, PM_DYNAMIC_QUERY_HANDLE *handle, PM_QUERY_ELEMENT *pElements, uint64_t numElements, double windowSizeMs, double /*offset*/) {
    if (numElements != 1 || windowSizeMs != 1000 || pElements->metric != PM_METRIC_PRESENTED_FPS || pElements->stat != PM_STAT_AVG) return PM_STATUS_BAD_ARGUMENT;
    pElements->dataOffset = mode() == "layout" ? 99999 : 8; pElements->dataSize = 8;
    *handle = &queryToken(); return PM_STATUS_SUCCESS;
}
PRESENTMON_API2_EXPORT PM_STATUS pmFreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE /*query*/) { return PM_STATUS_SUCCESS; }
PRESENTMON_API2_EXPORT PM_STATUS pmPollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE /*query*/, uint32_t /*pid*/, uint8_t *blob, uint32_t *numSwapChains) {
    if (mode() == "poll-error") return PM_STATUS_SERVICE_ERROR;
    if (mode() == "overcount") { *numSwapChains = 33; return PM_STATUS_SUCCESS; }
    if (*numSwapChains < 2) return PM_STATUS_INSUFFICIENT_BUFFER;
    *numSwapChains = 2;
    const double first = 120.0;
    const double last = mode() == "nan" ? std::numeric_limits<double>::quiet_NaN() : 144.0;
    const std::span<uint8_t> bytes(blob, 32);
    std::memcpy(bytes.subspan(8, 8).data(), &first, 8);
    std::memcpy(bytes.subspan(24, 8).data(), &last, 8);
    return PM_STATUS_SUCCESS;
}
}
