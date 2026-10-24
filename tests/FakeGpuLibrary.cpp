// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../third_party/amd_smi_abi.h"
#include "../daemon/GpuSensors.h"
#include <QtCore/qglobal.h>
#include <QByteArray>
#include <limits>
namespace {
int &deviceToken() { static int token = 0; return token; }
QByteArray mode() { return qgetenv("GALLEON_GPU_TEST_MODE"); }
}
extern "C" {
Q_DECL_EXPORT int nvmlInit_v2() { return mode() == "unsupported" ? 9 : 0; }
Q_DECL_EXPORT int nvmlShutdown() { return 0; }
Q_DECL_EXPORT int nvmlDeviceGetHandleByIndex_v2(unsigned int index, void **handle) {
    if (index != 0) return 2;
    *handle = &deviceToken(); return 0;
}
Q_DECL_EXPORT int nvmlDeviceGetTemperature(void * /*device*/, unsigned int sensor, unsigned int *value) {
    if (sensor != 0 || mode() == "partial") return 3;
    *value = mode() == "invalid" ? std::numeric_limits<unsigned int>::max() : 77; return 0;
}
Q_DECL_EXPORT int nvmlDeviceGetUtilizationRates(void * /*device*/, galleond::NvmlUtilization *value) {
    value->gpu = mode() == "invalid" ? 101 : 41;
    if (mode() == "zero") value->gpu = 0;
    value->memory = 99; return 0;
}
Q_DECL_EXPORT int amdsmi_init(uint64_t flags) { return flags == 2 && mode() != "unsupported" ? 0 : 1; }
Q_DECL_EXPORT int amdsmi_shut_down() { return 0; }
Q_DECL_EXPORT int amdsmi_get_socket_handles(uint32_t *count, void **handles) {
    if (mode() == "overcount") { *count = 99; return 0; }
    if (*count < 1) return 1;
    *count = 1; *handles = &deviceToken(); return 0;
}
Q_DECL_EXPORT int amdsmi_get_processor_handles(void * /*device*/, uint32_t *count, void **handles) {
    if (*count < 1) return 1;
    *count = 1; *handles = &deviceToken(); return 0;
}
Q_DECL_EXPORT int amdsmi_get_temp_metric(void * /*device*/, int sensor, int metric, int64_t *value) {
    if (sensor != 0 || metric != 0 || mode() == "partial") return 3;
    *value = mode() == "invalid" ? -1 : 55; return 0;
}
Q_DECL_EXPORT int amdsmi_get_gpu_activity(void * /*device*/, galleond::amd_smi_abi::EngineUsage *value) {
    *value = {}; value->gfx_activity = mode() == "invalid" ? 65535 : 73;
    if (mode() == "zero") value->gfx_activity = 0;
    value->umc_activity = 99; return 0;
}
}
