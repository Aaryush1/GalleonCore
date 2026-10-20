// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <SmtcController.h>

#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <chrono>
#include <thread>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace galleond;

namespace {
struct SessionState
{
    std::mutex mutex;
    SmtcSnapshot snapshot;
    std::vector<SmtcCommand> commands;
    bool fail = false;
    std::atomic<bool> blockCommand = false;
    std::atomic<bool> enteredCommand = false;
    std::atomic<bool> started = false;
    std::atomic<bool> stopped = false;
};
class FakeSession final : public SmtcSession
{
public:
    explicit FakeSession(std::shared_ptr<SessionState> state) : m_state(std::move(state)) {}
    void start() override { m_state->started = true; }
    void stop() noexcept override { m_state->stopped = true; }
    SmtcSnapshot read() override
    {
        const std::lock_guard lock(m_state->mutex);
        if (m_state->fail) throw std::runtime_error("disconnected");
        return m_state->snapshot;
    }
    void command(const SmtcCommand &command) override
    {
        m_state->enteredCommand = true;
        while (m_state->blockCommand.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const std::lock_guard lock(m_state->mutex);
        m_state->commands.push_back(command);
    }
private:
    std::shared_ptr<SessionState> m_state;
};
SmtcSnapshot playing()
{
    SmtcSnapshot result;
    result.state = MediaController::State::Playing;
    result.track.title = QStringLiteral("Song");
    result.session = 7;
    return result;
}
} // namespace

class TestSmtc : public QObject
{
    Q_OBJECT
private Q_SLOTS:
#ifdef Q_OS_WIN
    void nativeBackendCanShutDownWithoutAnInteractivePlayer()
    {
        const SmtcController controller(createNativeSmtcSession());
        QTest::qWait(100);
        // Headless CI may deny session access. Either result must shut down safely.
    }
#endif
    void noSessionMeansNoPlayerAndCommandsAreIgnored()
    {
        auto state = std::make_shared<SessionState>();
        SmtcController controller(std::make_unique<FakeSession>(state));
        QTRY_VERIFY(state->started.load());
        QCOMPARE(controller.state(), MediaController::State::NoPlayer);
        controller.next();
        const std::lock_guard lock(state->mutex);
        QVERIFY(state->commands.empty());
    }

    void sessionChangesAndDisappearanceArePublished()
    {
        auto state = std::make_shared<SessionState>();
        state->snapshot = playing();
        const SmtcController controller(std::make_unique<FakeSession>(state));
        const QSignalSpy changes(&controller, &MediaController::nowPlayingChanged);
        QTRY_COMPARE(controller.nowPlaying().title, QStringLiteral("Song"));
        {
            const std::lock_guard lock(state->mutex);
            state->snapshot.state = MediaController::State::Paused;
            state->snapshot.session = 8;
            state->snapshot.track.title = QStringLiteral("Other player");
            state->snapshot.shuffle = true;
            state->snapshot.loop = MediaController::Loop::Track;
        }
        QTRY_COMPARE(controller.state(), MediaController::State::Paused);
        QCOMPARE(controller.nowPlaying().title, QStringLiteral("Other player"));
        QVERIFY(controller.shuffleOn());
        QCOMPARE(controller.loopMode(), MediaController::Loop::Track);
        {
            const std::lock_guard lock(state->mutex);
            state->snapshot = {};
        }
        QTRY_COMPARE(controller.state(), MediaController::State::NoPlayer);
        QVERIFY(controller.nowPlaying().isEmpty());
        QVERIFY(changes.size() >= 3);
    }

    void commandsCarryTheObservedSessionAndSignedSeek()
    {
        auto state = std::make_shared<SessionState>();
        state->snapshot = playing();
        SmtcController controller(std::make_unique<FakeSession>(state));
        QTRY_COMPARE(controller.state(), MediaController::State::Playing);
        controller.playPause(); controller.next(); controller.previous(); controller.stop();
        controller.seek(-10); controller.toggleShuffle(); controller.cycleRepeat(); controller.restartTrack();
        auto count = [&] {
            const std::lock_guard lock(state->mutex);
            return state->commands.size();
        };
        QTRY_COMPARE(count(), std::size_t{8});
        const std::lock_guard lock(state->mutex);
        QCOMPARE(state->commands[0].action, SmtcAction::PlayPause);
        QCOMPARE(state->commands[1].action, SmtcAction::Next);
        QCOMPARE(state->commands[2].action, SmtcAction::Previous);
        QCOMPARE(state->commands[3].action, SmtcAction::Stop);
        QCOMPARE(state->commands[4].action, SmtcAction::Seek);
        QCOMPARE(state->commands[4].seconds, -10);
        QCOMPARE(state->commands[5].action, SmtcAction::Shuffle);
        QCOMPARE(state->commands[6].action, SmtcAction::Repeat);
        QCOMPARE(state->commands[7].action, SmtcAction::Restart);
        for (const auto &command : state->commands) QCOMPARE(command.session, std::uint64_t{7});
    }

    void invalidMetadataIsBoundedAndTimelineIsClamped()
    {
        auto state = std::make_shared<SessionState>();
        state->snapshot = playing();
        state->snapshot.track.title = QString(500, QLatin1Char('x'));
        state->snapshot.track.artist = QStringLiteral("a\nb");
        state->snapshot.track.durationSeconds = 30;
        state->snapshot.track.positionSeconds = 90;
        const SmtcController controller(std::make_unique<FakeSession>(state));
        QTRY_COMPARE(controller.state(), MediaController::State::Playing);
        QCOMPARE(controller.nowPlaying().title.size(), qsizetype{MaxMetadataLength});
        QCOMPARE(controller.nowPlaying().artist, QStringLiteral("a b"));
        QCOMPARE(controller.nowPlaying().positionSeconds, 30.0);
        {
            const std::lock_guard lock(state->mutex);
            state->snapshot.track.positionSeconds = std::numeric_limits<double>::quiet_NaN();
            state->snapshot.track.durationSeconds = -1;
        }
        QTRY_COMPARE(controller.nowPlaying().durationSeconds, 0.0);
        QCOMPARE(controller.nowPlaying().positionSeconds, 0.0);
    }

    void readFailureClearsTheTrackAndRecoveryWorks()
    {
        auto state = std::make_shared<SessionState>();
        state->snapshot = playing();
        const SmtcController controller(std::make_unique<FakeSession>(state));
        QTRY_COMPARE(controller.state(), MediaController::State::Playing);
        {
            const std::lock_guard lock(state->mutex);
            state->fail = true;
        }
        QTRY_COMPARE(controller.state(), MediaController::State::NoPlayer);
        QVERIFY(controller.nowPlaying().isEmpty());
        {
            const std::lock_guard lock(state->mutex);
            state->fail = false;
        }
        QTRY_COMPARE(controller.state(), MediaController::State::Playing);
    }

    void aSlowNativeCommandDoesNotBlockQtOrGrowTheQueue()
    {
        auto state = std::make_shared<SessionState>();
        state->snapshot = playing();
        SmtcController controller(std::make_unique<FakeSession>(state));
        struct Unblock
        {
            std::shared_ptr<SessionState> state;
            ~Unblock() { state->blockCommand = false; }
        } const unblock{state}; // releases the worker even if a Qt assertion returns early
        QTRY_COMPARE(controller.state(), MediaController::State::Playing);
        state->blockCommand = true;
        controller.next();
        QTRY_VERIFY(state->enteredCommand.load());
        for (int i = 0; i < 100; ++i) controller.next();
        bool eventDelivered = false;
        QMetaObject::invokeMethod(&controller, [&] { eventDelivered = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(eventDelivered);
        state->blockCommand = false;
        auto count = [&] {
            const std::lock_guard lock(state->mutex);
            return state->commands.size();
        };
        QTRY_COMPARE(count(), std::size_t{17}); // one executing, at most sixteen waiting
    }

    void shutdownJoinsTheWorkerAndStopsTheSession()
    {
        auto state = std::make_shared<SessionState>();
        {
            const SmtcController controller(std::make_unique<FakeSession>(state));
            QTRY_VERIFY(state->started.load());
        }
        QVERIFY(state->stopped.load());
    }
};
QTEST_GUILESS_MAIN(TestSmtc)
#include "test_smtc.moc"
