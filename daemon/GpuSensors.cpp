// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpuSensors.h"
#include "../third_party/amd_smi_abi.h"
#include <QLibrary>
#include <QScopeGuard>
#include <array>
#include <bit>
#include <span>
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
    static_assert(sizeof(unsigned int) == 4);
    const auto init = std::bit_cast<int (*)()>(library.resolve("nvmlInit_v2"));
    const auto shutdown = std::bit_cast<int (*)()>(library.resolve("nvmlShutdown"));
    const auto device = std::bit_cast<int (*)(unsigned int, Device *)>(library.resolve("nvmlDeviceGetHandleByIndex_v2"));
    const auto temperature = std::bit_cast<int (*)(Device, unsigned int, unsigned int *)>(library.resolve("nvmlDeviceGetTemperature"));
    const auto utilization = std::bit_cast<int (*)(Device, NvmlUtilization *)>(library.resolve("nvmlDeviceGetUtilizationRates"));
    if (!init || !shutdown || !device || (!temperature && !utilization) || init() != 0) return {};
    const auto cleanup = qScopeGuard([&] { shutdown(); });
    Device gpu = nullptr;
    if (device(0, &gpu) != 0 || !gpu) return {};
    QVariantMap result;
    unsigned int degrees = 0;
    if (temperature && temperature(gpu, 0, &degrees) == 0 && degrees > 0 && degrees < 150) result["gpu"] = double(degrees);
    NvmlUtilization usage{};
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
    const auto init = std::bit_cast<int (*)(uint64_t)>(library.resolve("amdsmi_init"));
    const auto shutdown = std::bit_cast<int (*)()>(library.resolve("amdsmi_shut_down"));
    const auto sockets = std::bit_cast<int (*)(uint32_t *, Handle *)>(library.resolve("amdsmi_get_socket_handles"));
    const auto processors = std::bit_cast<int (*)(Handle, uint32_t *, Handle *)>(library.resolve("amdsmi_get_processor_handles"));
    const auto temperature = std::bit_cast<int (*)(Handle, int, int, int64_t *)>(library.resolve("amdsmi_get_temp_metric"));
    const auto utilization = std::bit_cast<int (*)(Handle, EngineUsage *)>(library.resolve("amdsmi_get_gpu_activity"));
    if (!init || !shutdown || !sockets || !processors || (!temperature && !utilization) || init(InitGpus) != 0) return {};
    const auto cleanup = qScopeGuard([&] { shutdown(); });
    std::array<Handle, 32> socketHandles{};
    auto count = uint32_t(socketHandles.size());
    if (sockets(&count, socketHandles.data()) != 0 || count > socketHandles.size()) return {};
    for (auto *const socket : std::span(socketHandles).first(count)) {
        std::array<Handle, 32> devices{};
        auto deviceCount = uint32_t(devices.size());
        if (processors(socket, &deviceCount, devices.data()) != 0 || deviceCount > devices.size()) continue;
        for (auto *const device : std::span(devices).first(deviceCount)) {
            QVariantMap result;
            int64_t degrees = 0;
            if (temperature && temperature(device, TemperatureEdge, TemperatureCurrent, &degrees) == 0 && degrees > 0 && degrees < 150) result["gpu"] = double(degrees);
            EngineUsage usage{};
            if (utilization && utilization(device, &usage) == 0 && usage.gfx_activity <= 100) result["gpuLoad"] = double(usage.gfx_activity);
            if (!result.isEmpty()) { result["provider"] = QStringLiteral("AMD-SMI"); return result; }
        }
    }
    return {};
}
}
