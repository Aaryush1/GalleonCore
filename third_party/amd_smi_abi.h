/*
 * Copyright (c) Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// SPDX-License-Identifier: MIT
// Minimal read-only ABI from ROCm/amdsmi rocm-7.2.4, commit
// 1e91f3c1527617066f50c22f9ec4368fe82e1a3c, include/amd_smi/amdsmi.h.
// Keep reserved fields: the driver writes the entire structure.
#pragma once
#include <cstdint>
namespace galleond::amd_smi_abi {
using Handle = void *;
struct EngineUsage {
    uint32_t gfx_activity;
    uint32_t umc_activity;
    uint32_t mm_activity;
    uint32_t reserved[13];
};
static_assert(sizeof(EngineUsage) == 64);
constexpr uint64_t InitGpus = 1ULL << 1;
constexpr int TemperatureEdge = 0;
constexpr int TemperatureCurrent = 0;
}
