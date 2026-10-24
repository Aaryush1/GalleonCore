// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../third_party/PresentMonAPI.h"
#include <QtCore/qglobal.h>
#include <QByteArray>
#include <cstring>
#include <limits>
namespace {
int token = 0;
QByteArray mode() { return qgetenv("GALLEON_PM_TEST_MODE"); }
}
extern "C" {
PRESENTMON_API2_EXPORT PM_STATUS pmGetApiVersion(PM_VERSION *version) {
    *version = {}; version->major = uint16_t(mode() == "version" ? 99 : PM_API_VERSION_MAJOR); return PM_STATUS_SUCCESS;
}
PRESENTMON_API2_EXPORT PM_STATUS pmOpenSession(PM_SESSION_HANDLE *handle) {
    if (mode() == "offline") return PM_STATUS_PIPE_ERROR;
    *handle = reinterpret_cast<PM_SESSION_HANDLE>(&token); return PM_STATUS_SUCCESS;
}
PRESENTMON_API2_EXPORT PM_STATUS pmCloseSession(PM_SESSION_HANDLE) { return PM_STATUS_SUCCESS; }
PRESENTMON_API2_EXPORT PM_STATUS pmStartTrackingProcess(PM_SESSION_HANDLE, uint32_t pid) { return pid ? PM_STATUS_SUCCESS : PM_STATUS_INVALID_PID; }
PRESENTMON_API2_EXPORT PM_STATUS pmStopTrackingProcess(PM_SESSION_HANDLE, uint32_t) { return PM_STATUS_SUCCESS; }
PRESENTMON_API2_EXPORT PM_STATUS pmRegisterDynamicQuery(PM_SESSION_HANDLE, PM_DYNAMIC_QUERY_HANDLE *handle, PM_QUERY_ELEMENT *element, uint64_t count, double window, double) {
    if (count != 1 || window != 1000 || element->metric != PM_METRIC_PRESENTED_FPS || element->stat != PM_STAT_AVG) return PM_STATUS_BAD_ARGUMENT;
    element->dataOffset = mode() == "layout" ? 99999 : 8; element->dataSize = 8;
    *handle = reinterpret_cast<PM_DYNAMIC_QUERY_HANDLE>(&token); return PM_STATUS_SUCCESS;
}
PRESENTMON_API2_EXPORT PM_STATUS pmFreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE) { return PM_STATUS_SUCCESS; }
PRESENTMON_API2_EXPORT PM_STATUS pmPollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE, uint32_t, uint8_t *blob, uint32_t *swaps) {
    if (mode() == "poll-error") return PM_STATUS_SERVICE_ERROR;
    if (mode() == "overcount") { *swaps = 33; return PM_STATUS_SUCCESS; }
    if (*swaps < 2) return PM_STATUS_INSUFFICIENT_BUFFER;
    *swaps = 2;
    const double first = 120.0, last = mode() == "nan" ? std::numeric_limits<double>::quiet_NaN() : 144.0;
    std::memcpy(blob + 8, &first, 8); std::memcpy(blob + 24, &last, 8);
    return PM_STATUS_SUCCESS;
}
}
