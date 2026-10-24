// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../third_party/amd_smi_abi.h"
#include <QtCore/qglobal.h>
#include <QByteArray>
#include <limits>
namespace {
int deviceToken = 0;
QByteArray mode() { return qgetenv("GALLEON_GPU_TEST_MODE"); }
struct Utilization { unsigned int gpu, memory; };
}
extern "C" {
Q_DECL_EXPORT int nvmlInit_v2() { return mode() == "unsupported" ? 9 : 0; }
Q_DECL_EXPORT int nvmlShutdown() { return 0; }
Q_DECL_EXPORT int nvmlDeviceGetHandleByIndex_v2(unsigned int index, void **handle) {
    if (index != 0) return 2;
    *handle = &deviceToken; return 0;
}
Q_DECL_EXPORT int nvmlDeviceGetTemperature(void *, unsigned int sensor, unsigned int *value) {
    if (sensor != 0 || mode() == "partial") return 3;
    *value = mode() == "invalid" ? std::numeric_limits<unsigned int>::max() : 77; return 0;
}
Q_DECL_EXPORT int nvmlDeviceGetUtilizationRates(void *, Utilization *value) {
    value->gpu = mode() == "invalid" ? 101 : mode() == "zero" ? 0 : 41;
    value->memory = 99; return 0;
}
Q_DECL_EXPORT int amdsmi_init(uint64_t flags) { return flags == 2 && mode() != "unsupported" ? 0 : 1; }
Q_DECL_EXPORT int amdsmi_shut_down() { return 0; }
Q_DECL_EXPORT int amdsmi_get_socket_handles(uint32_t *count, void **handles) {
    if (mode() == "overcount") { *count = 99; return 0; }
    if (*count < 1) return 1;
    *count = 1; handles[0] = &deviceToken; return 0;
}
Q_DECL_EXPORT int amdsmi_get_processor_handles(void *, uint32_t *count, void **handles) {
    if (*count < 1) return 1;
    *count = 1; handles[0] = &deviceToken; return 0;
}
Q_DECL_EXPORT int amdsmi_get_temp_metric(void *, int sensor, int metric, int64_t *value) {
    if (sensor != 0 || metric != 0 || mode() == "partial") return 3;
    *value = mode() == "invalid" ? -1 : 55; return 0;
}
Q_DECL_EXPORT int amdsmi_get_gpu_activity(void *, galleond::amd_smi_abi::EngineUsage *value) {
    *value = {}; value->gfx_activity = mode() == "invalid" ? 65535 : mode() == "zero" ? 0 : 73;
    value->umc_activity = 99; return 0;
}
}
