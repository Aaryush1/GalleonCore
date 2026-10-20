// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SmtcSession.h"
#include "SmtcController.h"
#include "PlatformBackends.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <QCryptographicHash>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace galleond {
namespace {
using namespace winrt::Windows::Media::Control;
using winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
using winrt::Windows::Foundation::AsyncStatus;

// Bound every asynchronous wait on the MTA worker, never on the Qt thread.
template<class Operation>
auto awaitResult(const Operation &operation)
{
    if (operation.wait_for(std::chrono::milliseconds(500)) != AsyncStatus::Completed) {
        operation.Cancel();
        throw std::runtime_error("SMTC operation did not complete");
    }
    return operation.GetResults();
}

QString text(const winrt::hstring &value)
{
    // Bound conversion before allocating a Qt string; the controller sanitizes it.
    return QString::fromWCharArray(value.c_str(), static_cast<int>(std::min<std::size_t>(value.size(), MaxMetadataLength)));
}

class WindowsSmtcSession final : public SmtcSession
{
public:
    void start() override
    {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            m_initialized = true;
        } catch (const winrt::hresult_error &) {
            throw std::runtime_error("SMTC apartment initialization failed");
        }
    }
    void stop() override
    {
        m_current = nullptr;
        m_manager = nullptr;
        if (m_initialized)
            winrt::uninit_apartment();
        m_initialized = false;
    }

    SmtcSnapshot read() override
    {
        try {
            if (!m_manager)
                m_manager = awaitResult(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            const auto session = m_manager.GetCurrentSession();
            if (session != m_current) {
                m_current = session;
                ++m_generation;
                m_artKey.clear();
                m_artUrl.clear();
            }
            if (!session)
                return {};
            const auto playback = session.GetPlaybackInfo();
            const auto properties = awaitResult(session.TryGetMediaPropertiesAsync());
            const auto timeline = session.GetTimelineProperties();
            SmtcSnapshot result;
            result.session = m_generation;
            switch (playback.PlaybackStatus()) {
            case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
                result.state = MediaController::State::Playing; break;
            case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
                result.state = MediaController::State::Paused; break;
            default: result.state = MediaController::State::Stopped; break;
            }
            result.track.title = text(properties.Title());
            result.track.artist = text(properties.Artist());
            const auto start = timeline.StartTime().count();
            result.track.durationSeconds = std::max(0.0, (static_cast<double>(timeline.EndTime().count()) - static_cast<double>(start)) / 10000000.0);
            result.track.positionSeconds = std::max(0.0, (static_cast<double>(timeline.Position().count()) - static_cast<double>(start)) / 10000000.0);
            const auto shuffle = playback.IsShuffleActive();
            result.shuffle = shuffle && shuffle.Value();
            const auto repeat = playback.AutoRepeatMode();
            if (repeat && repeat.Value() == MediaPlaybackAutoRepeatMode::Track)
                result.loop = MediaController::Loop::Track;
            else if (repeat && repeat.Value() == MediaPlaybackAutoRepeatMode::List)
                result.loop = MediaController::Loop::Playlist;
            result.track.artUrl = artwork(properties);
            // A session switch during an asynchronous read must not publish old data.
            if (m_manager.GetCurrentSession() != session)
                return {};
            return result;
        } catch (const winrt::hresult_error &) {
            m_current = nullptr;
            m_manager = nullptr;
            m_artKey.clear();
            m_artUrl.clear();
            throw std::runtime_error("SMTC read failed");
        }
    }

    void command(const SmtcCommand &command) override
    {
        try {
            if (!m_manager || !m_current || command.session != m_generation
                || m_manager.GetCurrentSession() != m_current)
                return;
            const auto info = m_current.GetPlaybackInfo();
            const auto controls = info.Controls();
            switch (command.action) {
            case SmtcAction::PlayPause:
                if (controls.IsPlayPauseToggleEnabled())
                    (void)awaitResult(m_current.TryTogglePlayPauseAsync());
                break;
            case SmtcAction::Next:
                if (controls.IsNextEnabled()) (void)awaitResult(m_current.TrySkipNextAsync());
                break;
            case SmtcAction::Previous:
                if (controls.IsPreviousEnabled()) (void)awaitResult(m_current.TrySkipPreviousAsync());
                break;
            case SmtcAction::Stop:
                if (controls.IsStopEnabled()) (void)awaitResult(m_current.TryStopAsync());
                break;
            case SmtcAction::Seek:
            case SmtcAction::Restart:
                if (controls.IsPlaybackPositionEnabled()) {
                    const auto timeline = m_current.GetTimelineProperties();
                    const auto low = std::max<std::int64_t>(0, timeline.MinSeekTime().count());
                    const auto high = timeline.MaxSeekTime().count();
                    if (high < low) break;
                    auto position = std::clamp(timeline.Position().count(), low, high);
                    const auto delta = static_cast<std::int64_t>(command.seconds) * 10000000;
                    if (command.action == SmtcAction::Restart)
                        position = low;
                    else if (delta > 0)
                        position += std::min(delta, high - position);
                    else
                        position -= std::min(-delta, position - low);
                    (void)awaitResult(m_current.TryChangePlaybackPositionAsync(position));
                }
                break;
            case SmtcAction::Shuffle:
                if (controls.IsShuffleEnabled()) {
                    const auto active = info.IsShuffleActive();
                    (void)awaitResult(m_current.TryChangeShuffleActiveAsync(!(active && active.Value())));
                }
                break;
            case SmtcAction::Repeat:
                if (controls.IsRepeatEnabled()) {
                    const auto mode = info.AutoRepeatMode();
                    const auto current = mode ? mode.Value() : MediaPlaybackAutoRepeatMode::None;
                    const auto next = current == MediaPlaybackAutoRepeatMode::None ? MediaPlaybackAutoRepeatMode::List
                                    : current == MediaPlaybackAutoRepeatMode::List ? MediaPlaybackAutoRepeatMode::Track
                                    : MediaPlaybackAutoRepeatMode::None;
                    (void)awaitResult(m_current.TryChangeAutoRepeatModeAsync(next));
                }
                break;
            }
        } catch (const winrt::hresult_error &) {
            throw std::runtime_error("SMTC command failed");
        }
    }

private:
    QString artwork(const GlobalSystemMediaTransportControlsSessionMediaProperties &properties)
    {
        // One bounded file, replaced atomically; fragment identifies changed content
        // for consumers caching by URL. CoverLoader validates image format/dimensions.
        try {
            const auto reference = properties.Thumbnail();
            if (!reference || !m_artDir.isValid()) return {};
            const auto stream = awaitResult(reference.OpenReadAsync());
            const auto size = stream.Size();
            constexpr std::uint32_t MaxBytes = 2 * 1024 * 1024;
            if (size == 0 || size > MaxBytes) return {};
            winrt::Windows::Storage::Streams::DataReader reader(stream);
            const auto count = static_cast<std::uint32_t>(size);
            if (awaitResult(reader.LoadAsync(count)) != count) return {};
            QByteArray bytes(static_cast<qsizetype>(count), Qt::Uninitialized);
            reader.ReadBytes(winrt::array_view<std::uint8_t>(reinterpret_cast<std::uint8_t *>(bytes.data()), count));
            const auto key = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
            if (key == m_artKey) return m_artUrl;
            QSaveFile file(m_artDir.filePath(QStringLiteral("cover.img")));
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return {};
            QUrl url = QUrl::fromLocalFile(file.fileName());
            url.setFragment(QString::fromLatin1(key));
            m_artKey = key;
            m_artUrl = url.toString();
            return m_artUrl;
        } catch (const winrt::hresult_error &) {
            return {};
        } catch (const std::exception &) {
            return {};
        }
    }

    bool m_initialized = false;
    GlobalSystemMediaTransportControlsSessionManager m_manager{nullptr};
    GlobalSystemMediaTransportControlsSession m_current{nullptr};
    std::uint64_t m_generation = 0;
    QTemporaryDir m_artDir;
    QByteArray m_artKey;
    QString m_artUrl;
};
} // namespace

std::unique_ptr<SmtcSession> createNativeSmtcSession()
{
    return std::make_unique<WindowsSmtcSession>();
}
std::unique_ptr<MediaController> createMediaController()
{
    return std::make_unique<SmtcController>(createNativeSmtcSession());
}
} // namespace galleond
