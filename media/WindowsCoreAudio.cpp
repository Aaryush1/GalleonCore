// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AudioEndpoint.h"

#include <winrt/base.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include <stdexcept>

namespace galleond {
namespace {
void checkAudioResult(HRESULT result)
{
    if (FAILED(result))
        throw std::runtime_error("Windows audio endpoint unavailable");
}

class WindowsAudioEndpoint final : public AudioEndpoint
{
public:
    WindowsAudioEndpoint()
    {
        winrt::com_ptr<IMMDeviceEnumerator> enumerator;
        checkAudioResult(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                         __uuidof(IMMDeviceEnumerator), enumerator.put_void()));
        winrt::com_ptr<IMMDevice> device;
        checkAudioResult(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put()));
        checkAudioResult(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                                         nullptr, m_volume.put_void()));
    }

    float volume() override
    {
        float result = 0.0F;
        checkAudioResult(m_volume->GetMasterVolumeLevelScalar(&result));
        return result;
    }
    void setVolume(float scalar) override
    {
        checkAudioResult(m_volume->SetMasterVolumeLevelScalar(scalar, nullptr));
    }
    bool muted() override
    {
        BOOL result = FALSE;
        checkAudioResult(m_volume->GetMute(&result));
        return result != FALSE;
    }
    void setMuted(bool muted) override
    {
        checkAudioResult(m_volume->SetMute(muted ? TRUE : FALSE, nullptr));
    }

private:
    winrt::com_ptr<IAudioEndpointVolume> m_volume;
};
} // namespace

std::unique_ptr<AudioEndpoint> createDefaultAudioEndpoint()
{
    return std::make_unique<WindowsAudioEndpoint>();
}
} // namespace galleond
