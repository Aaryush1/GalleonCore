// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpuSensors.h"
#include "../third_party/amd_smi_abi.h"
#include <QLibrary>
#include <QScopeGuard>
#include <array>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace galleond {
namespace {
QStringList nvmlLibraries() {
#ifdef Q_OS_WIN
    QStringList result;
    wchar_t system[MAX_PATH];
    const auto length = GetSystemDirectoryW(system, MAX_PATH);
    if (length && length < MAX_PATH) result << QString::fromWCharArray(system) + QStringLiteral("/nvml.dll");
    const auto programs = qEnvironmentVariable("ProgramW6432");
    if (!programs.isEmpty()) result << programs + QStringLiteral("/NVIDIA Corporation/NVSMI/nvml.dll");
    return result;
#else
    return {QStringLiteral("libnvidia-ml.so.1"), QStringLiteral("libnvidia-ml.so")};
#endif
}
bool load(QLibrary &library, const QStringList &candidates) {
    for (const auto &path : candidates) { library.setFileName(path); if (library.load()) return true; }
    return false;
}
}
QVariantMap readNvmlSensors(const QStringList &libraries) {
    QLibrary library;
    if (!load(library, libraries.isEmpty() ? nvmlLibraries() : libraries)) return {};
    using Device = void *;
    // Stable NVML C ABI: nvmlUtilization_t has two unsigned integer fields.
    struct Utilization { unsigned int gpu, memory; };
    static_assert(sizeof(unsigned int) == 4);
    const auto init = reinterpret_cast<int (*)()>(library.resolve("nvmlInit_v2"));
    const auto shutdown = reinterpret_cast<int (*)()>(library.resolve("nvmlShutdown"));
    const auto device = reinterpret_cast<int (*)(unsigned int, Device *)>(library.resolve("nvmlDeviceGetHandleByIndex_v2"));
    const auto temperature = reinterpret_cast<int (*)(Device, unsigned int, unsigned int *)>(library.resolve("nvmlDeviceGetTemperature"));
    const auto utilization = reinterpret_cast<int (*)(Device, Utilization *)>(library.resolve("nvmlDeviceGetUtilizationRates"));
    if (!init || !shutdown || !device || (!temperature && !utilization) || init() != 0) return {};
    const auto cleanup = qScopeGuard([&] { shutdown(); });
    Device gpu = nullptr;
    if (device(0, &gpu) != 0 || !gpu) return {};
    QVariantMap result;
    unsigned int degrees = 0;
    if (temperature && temperature(gpu, 0, &degrees) == 0 && degrees > 0 && degrees < 150) result["gpu"] = double(degrees);
    Utilization usage{};
    if (utilization && utilization(gpu, &usage) == 0 && usage.gpu <= 100) result["gpuLoad"] = double(usage.gpu);
    if (!result.isEmpty()) result["provider"] = QStringLiteral("NVML");
    return result;
}
QVariantMap readAmdSmiSensors(const QStringList &libraries) {
    QLibrary library;
    const QStringList defaults{QStringLiteral("libamd_smi.so"), QStringLiteral("libamd_smi.so.1"),
        QStringLiteral("/opt/rocm/lib/libamd_smi.so"), QStringLiteral("/opt/rocm/lib64/libamd_smi.so")};
    if (!load(library, libraries.isEmpty() ? defaults : libraries)) return {};
    using namespace amd_smi_abi;
    const auto init = reinterpret_cast<int (*)(uint64_t)>(library.resolve("amdsmi_init"));
    const auto shutdown = reinterpret_cast<int (*)()>(library.resolve("amdsmi_shut_down"));
    const auto sockets = reinterpret_cast<int (*)(uint32_t *, Handle *)>(library.resolve("amdsmi_get_socket_handles"));
    const auto processors = reinterpret_cast<int (*)(Handle, uint32_t *, Handle *)>(library.resolve("amdsmi_get_processor_handles"));
    const auto temperature = reinterpret_cast<int (*)(Handle, int, int, int64_t *)>(library.resolve("amdsmi_get_temp_metric"));
    const auto utilization = reinterpret_cast<int (*)(Handle, EngineUsage *)>(library.resolve("amdsmi_get_gpu_activity"));
    if (!init || !shutdown || !sockets || !processors || (!temperature && !utilization) || init(InitGpus) != 0) return {};
    const auto cleanup = qScopeGuard([&] { shutdown(); });
    std::array<Handle, 32> socketHandles{};
    uint32_t count = uint32_t(socketHandles.size());
    if (sockets(&count, socketHandles.data()) != 0 || count > socketHandles.size()) return {};
    for (uint32_t i = 0; i < count; ++i) {
        std::array<Handle, 32> devices{};
        uint32_t deviceCount = uint32_t(devices.size());
        if (processors(socketHandles[i], &deviceCount, devices.data()) != 0 || deviceCount > devices.size()) continue;
        for (uint32_t j = 0; j < deviceCount; ++j) {
            QVariantMap result;
            int64_t degrees = 0;
            if (temperature && temperature(devices[j], TemperatureEdge, TemperatureCurrent, &degrees) == 0 && degrees > 0 && degrees < 150) result["gpu"] = double(degrees);
            EngineUsage usage{};
            if (utilization && utilization(devices[j], &usage) == 0 && usage.gfx_activity <= 100) result["gpuLoad"] = double(usage.gfx_activity);
            if (!result.isEmpty()) { result["provider"] = QStringLiteral("AMD-SMI"); return result; }
        }
    }
    return {};
}
}
