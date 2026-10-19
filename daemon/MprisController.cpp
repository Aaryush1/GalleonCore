// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MprisController.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusArgument>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace galleond {

namespace {

constexpr int SystemVolumeGatherMs = 40; // steps of a fast turn are gathered for this long
constexpr int NamesIntervalMs = 3000;
constexpr int StateIntervalMs = 1000;
constexpr int CallTimeoutMs = 1500;
constexpr int ChangeDelayMs = 15; // bursts of notifications are read once
constexpr int MaxPlayers = 16; // a session never has this many: bounds the polling

const QString PlayerPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString PlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString RootInterface = QStringLiteral("org.mpris.MediaPlayer2");
const QString PropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

// "xesam:artist" is a list of strings by the specification, but some players
// send a single string: both are accepted.
QString artistOf(const QVariant &value)
{
    QStringList artists;
    if (value.metaType().id() == QMetaType::QString) {
        artists << value.toString();
    } else if (value.canConvert<QDBusArgument>()) {
        artists = qdbus_cast<QStringList>(value.value<QDBusArgument>());
    } else {
        artists = value.toStringList();
    }
    artists.removeAll(QString());
    return cleanMetadataText(artists.join(QStringLiteral(", ")));
}

// A readable title for a track that has none (a video, an untagged file): the
// name of the file or of the last part of the address, without its extension.
QString titleFromUrl(const QString &address)
{
    const QUrl url(address);
    if (!url.isValid())
        return {};
    QString name = QFileInfo(url.fileName()).completeBaseName(); // fileName() is already percent-decoded
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    return cleanMetadataText(name);
}

// The track from the answer of GetAll on org.mpris.MediaPlayer2.Player.
NowPlaying trackFrom(const QVariantMap &properties)
{
    NowPlaying track;
    QVariantMap metadata;
    const QVariant raw = properties.value(QStringLiteral("Metadata"));
    if (raw.canConvert<QDBusArgument>())
        metadata = qdbus_cast<QVariantMap>(raw.value<QDBusArgument>());
    else
        metadata = raw.toMap();

    track.title = cleanMetadataText(metadata.value(QStringLiteral("xesam:title")).toString());
    track.artist = artistOf(metadata.value(QStringLiteral("xesam:artist")));
    track.artUrl = metadata.value(QStringLiteral("mpris:artUrl")).toString().left(2048).trimmed();
    track.url = metadata.value(QStringLiteral("xesam:url")).toString().left(2048).trimmed();
    // A video or an untagged file has no title: its file name stands in.
    if (track.title.isEmpty() && track.artist.isEmpty())
        track.title = titleFromUrl(track.url);

    // Microseconds, signed or unsigned depending on the player.
    bool ok = false;
    const qlonglong length = metadata.value(QStringLiteral("mpris:length")).toLongLong(&ok);
    if (ok && length > 0)
        track.durationSeconds = static_cast<double>(length) / 1e6;
    const qlonglong position = properties.value(QStringLiteral("Position")).toLongLong(&ok);
    if (ok && position > 0)
        track.positionSeconds = static_cast<double>(position) / 1e6;
    if (track.durationSeconds > 0 && track.positionSeconds > track.durationSeconds)
        track.positionSeconds = track.durationSeconds;
    return track;
}

MediaController::State stateFromStatus(const QString &status)
{
    if (status == QLatin1String("Playing"))
        return MediaController::State::Playing;
    if (status == QLatin1String("Paused"))
        return MediaController::State::Paused;
    return MediaController::State::Stopped;
}

} // namespace

MprisController::MprisController(QDBusConnection bus, QString namePrefix, QObject *parent)
    : MediaController(parent)
    , m_bus(std::move(bus))
    , m_prefix(std::move(namePrefix))
{
    m_clock.start();
    // The system volume is only touched by the real thing: a controller made with
    // another name prefix (the tests) never moves the volume of the machine it runs on.
    if (m_prefix == QLatin1String("org.mpris.MediaPlayer2."))
        m_systemVolumeProgram = QStandardPaths::findExecutable(QStringLiteral("wpctl"));
    m_systemVolumeTimer.setSingleShot(true);
    m_systemVolumeTimer.setInterval(SystemVolumeGatherMs);
    connect(&m_systemVolumeTimer, &QTimer::timeout, this, [this] {
        const int percent = std::exchange(m_pendingSystemVolume, 0);
        if (percent == 0 || m_systemVolumeProgram.isEmpty())
            return;
        // A fixed command and a whole number: nothing from outside goes into it. At most 100 %.
        QProcess::startDetached(m_systemVolumeProgram,
                                {QStringLiteral("set-volume"), QStringLiteral("-l"), QStringLiteral("1.0"),
                                 QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                                 QString::number(std::abs(percent)) + QLatin1Char('%') + (percent > 0 ? QLatin1Char('+') : QLatin1Char('-'))});
    });
    m_namesTimer.setInterval(NamesIntervalMs);
    m_stateTimer.setInterval(StateIntervalMs);
    connect(&m_namesTimer, &QTimer::timeout, this, &MprisController::refreshNames);
    connect(&m_stateTimer, &QTimer::timeout, this, &MprisController::setPlayers);
    m_namesTimer.start();
    m_stateTimer.start();

    // The polling above only keeps the timeline moving and catches what a player
    // forgets to announce; a change of track or status arrives as a notification.
    m_changeTimer.setSingleShot(true);
    m_changeTimer.setInterval(ChangeDelayMs);
    connect(&m_changeTimer, &QTimer::timeout, this, &MprisController::setPlayers);
    m_bus.connect(QString(), PlayerPath, PropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                  SLOT(onPropertiesChanged(QDBusMessage)));
    m_bus.connect(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                  QStringLiteral("org.freedesktop.DBus"), QStringLiteral("NameOwnerChanged"), this,
                  SLOT(onNameOwnerChanged(QString, QString, QString)));
    refreshNow();
}

MediaController::State MprisController::state() const
{
    const QString name = current();
    return name.isEmpty() ? State::NoPlayer : m_players.value(name).state;
}

NowPlaying MprisController::nowPlaying() const
{
    const QString name = current();
    if (name.isEmpty())
        return {};
    const Player player = m_players.value(name);
    NowPlaying track = player.track;
    track.playerIcon = player.iconName;
    return track;
}

QString MprisController::iconNameForDesktopEntry(const QString &desktopEntry)
{
    static const QRegularExpression plainName(QStringLiteral("^[A-Za-z0-9._-]{1,100}$"));
    if (!plainName.match(desktopEntry).hasMatch())
        return {};
    // The application's own file says which icon it uses.
    const QString file = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                QStringLiteral("applications/") + desktopEntry + QStringLiteral(".desktop"));
    if (!file.isEmpty()) {
        QSettings settings(file, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Desktop Entry"));
        const QString icon = settings.value(QStringLiteral("Icon")).toString();
        if (plainName.match(icon).hasMatch()) // a name of the theme; a path is not used
            return icon;
    }
    return desktopEntry; // most applications name their icon like their entry
}

void MprisController::fetchIdentity(const QString &name)
{
    // The root interface (org.mpris.MediaPlayer2) names the application.
    QDBusMessage message = QDBusMessage::createMethodCall(name, PlayerPath, PropertiesInterface, QStringLiteral("GetAll"));
    message << QStringLiteral("org.mpris.MediaPlayer2");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *call;
        auto player = m_players.find(name);
        if (reply.isError() || player == m_players.end())
            return;
        const QString entry = reply.value().value(QStringLiteral("DesktopEntry")).toString().left(100);
        player->iconName = iconNameForDesktopEntry(entry);
    });
}

QString MprisController::current() const
{
    QString best;
    for (auto it = m_players.constBegin(); it != m_players.constEnd(); ++it) {
        if (best.isEmpty()) {
            best = it.key();
            continue;
        }
        const Player &candidate = it.value();
        const Player &chosen = m_players.value(best);
        const bool candidatePlaying = candidate.state == State::Playing;
        const bool chosenPlaying = chosen.state == State::Playing;
        if (candidatePlaying != chosenPlaying ? candidatePlaying
            : candidate.lastPlaying != chosen.lastPlaying ? candidate.lastPlaying > chosen.lastPlaying
                                                          : it.key() < best)
            best = it.key();
    }
    return best;
}

void MprisController::setPollIntervals(int namesMs, int stateMs)
{
    m_namesTimer.setInterval(namesMs);
    m_stateTimer.setInterval(stateMs);
}

void MprisController::onPropertiesChanged(const QDBusMessage &message)
{
    // Only the player interface matters; a track change sends several of these
    // at once, so they are joined and the players are read one time.
    if (message.arguments().value(0).toString() == PlayerInterface && !m_players.isEmpty())
        m_changeTimer.start();
}

void MprisController::onNameOwnerChanged(const QString &name, const QString &, const QString &)
{
    if (name.startsWith(m_prefix))
        refreshNames();
}

void MprisController::refreshNow()
{
    refreshNames();
}

void MprisController::refreshNames()
{
    if (!m_bus.isConnected())
        return;
    // ListNames asynchronously (registeredServiceNames() would block).
    const QDBusMessage listNames = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                                  QStringLiteral("/org/freedesktop/DBus"),
                                                                  QStringLiteral("org.freedesktop.DBus"),
                                                                  QStringLiteral("ListNames"));
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(listNames, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QStringList> reply = *call;
        if (reply.isError())
            return;

        QStringList found;
        for (const QString &name : reply.value()) {
            if (name.startsWith(m_prefix) && !name.endsWith(QLatin1String(".playerctld")) && found.size() < MaxPlayers)
                found.append(name);
        }
        for (auto it = m_players.begin(); it != m_players.end();) {
            if (found.contains(it.key())) {
                ++it;
            } else {
                qInfo() << "galleond: music player gone:" << it.key();
                it = m_players.erase(it);
            }
        }
        for (const QString &name : std::as_const(found)) {
            if (!m_players.contains(name)) {
                qInfo() << "galleond: music player found:" << name;
                m_players.insert(name, Player{});
                fetchIdentity(name);
            }
        }
        setPlayers(); // reads the states, and reports a change
    });
}

void MprisController::setPlayers()
{
    // Reads the state of every known player, then reports a change if the
    // answer of the current one is different from what was last reported.
    for (auto it = m_players.constBegin(); it != m_players.constEnd(); ++it)
        refreshState(it.key());
    if (m_players.isEmpty() && m_reported != State::NoPlayer) {
        m_reported = State::NoPlayer;
        m_reportedTrack = {};
        Q_EMIT stateChanged();
        Q_EMIT nowPlayingChanged();
    }
}

void MprisController::refreshState(const QString &name)
{
    // One GetAll gives the status, the metadata and the position together.
    QDBusMessage message = QDBusMessage::createMethodCall(name, PlayerPath, PropertiesInterface, QStringLiteral("GetAll"));
    message << PlayerInterface;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        auto player = m_players.find(name);
        if (player == m_players.end())
            return;
        const QDBusPendingReply<QVariantMap> reply = *call;
        if (reply.isError()) {
            if (player->state != State::Stopped || !player->track.isEmpty())
                qWarning() << "galleond: cannot read the player" << name << reply.error().message();
            player->state = State::Stopped;
            player->track = {};
        } else {
            const QVariantMap properties = reply.value();
            player->state = stateFromStatus(properties.value(QStringLiteral("PlaybackStatus")).toString());
            player->track = trackFrom(properties);
        }
        if (player->state == State::Playing)
            player->lastPlaying = m_clock.elapsed();

        const State now = state();
        const NowPlaying track = nowPlaying();
        const bool stateChanged = now != m_reported;
        const bool trackChanged = !(track == m_reportedTrack);
        if (stateChanged || track.title != m_reportedTrack.title || track.artist != m_reportedTrack.artist
            || track.artUrl != m_reportedTrack.artUrl) {
            static const char *names[] = {"no player", "stopped", "paused", "playing"};
            qInfo().noquote() << "galleond: player" << names[static_cast<int>(now)] << "-" << track.artist << "-" << track.title
                              << (track.artUrl.isEmpty() ? QStringLiteral("(no cover address)") : QStringLiteral("cover: ") + track.artUrl.left(100));
        }
        m_reported = now;
        m_reportedTrack = track;
        if (stateChanged)
            Q_EMIT this->stateChanged();
        if (stateChanged || trackChanged)
            Q_EMIT nowPlayingChanged();
    });
}

void MprisController::callPlayer(const QString &method, const QVariantList &arguments)
{
    const QString name = current();
    if (name.isEmpty())
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(name, PlayerPath, PlayerInterface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name, method](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError())
            qWarning() << "galleond: the player" << name << "refused" << method << call->error().message();
        refreshState(name); // the state changed (or not): the icon must follow
    });
}

void MprisController::playPause()
{
    callPlayer(QStringLiteral("PlayPause"));
}

void MprisController::next()
{
    callPlayer(QStringLiteral("Next"));
}

void MprisController::previous()
{
    callPlayer(QStringLiteral("Previous"));
}

void MprisController::stop()
{
    callPlayer(QStringLiteral("Stop"));
}

void MprisController::changeProperty(const QString &interface, const QString &property,
                                     const std::function<QVariant(const QVariant &)> &next)
{
    const QString name = current();
    if (name.isEmpty())
        return;
    QDBusMessage get = QDBusMessage::createMethodCall(name, PlayerPath, PropertiesInterface, QStringLiteral("Get"));
    get << interface << property;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name, interface, property, next](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *call;
        if (reply.isError())
            return; // the player has no such property: nothing to change
        const QVariant value = next(reply.value().variant());
        if (!value.isValid())
            return;
        QDBusMessage set = QDBusMessage::createMethodCall(name, PlayerPath, PropertiesInterface, QStringLiteral("Set"));
        set << interface << property << QVariant::fromValue(QDBusVariant(value));
        auto *setWatcher = new QDBusPendingCallWatcher(m_bus.asyncCall(set, CallTimeoutMs), this);
        connect(setWatcher, &QDBusPendingCallWatcher::finished, this, [name, property](QDBusPendingCallWatcher *done) {
            done->deleteLater();
            if (done->isError())
                qWarning() << "galleond: the player" << name << "refused to change" << property << done->error().message();
        });
    });
}

void MprisController::toggleShuffle()
{
    changeProperty(PlayerInterface, QStringLiteral("Shuffle"), [](const QVariant &v) { return QVariant(!v.toBool()); });
}

void MprisController::cycleRepeat()
{
    // None -> the whole playlist -> the track -> none, as a player's own repeat button does.
    changeProperty(PlayerInterface, QStringLiteral("LoopStatus"), [](const QVariant &v) {
        const QString now = v.toString();
        return QVariant(now == QLatin1String("None") ? QStringLiteral("Playlist")
                        : now == QLatin1String("Playlist") ? QStringLiteral("Track") : QStringLiteral("None"));
    });
}

void MprisController::toggleFullscreen()
{
    changeProperty(RootInterface, QStringLiteral("Fullscreen"), [](const QVariant &v) { return QVariant(!v.toBool()); });
}

void MprisController::raisePlayer()
{
    const QString name = current();
    if (name.isEmpty())
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(name, PlayerPath, RootInterface, QStringLiteral("Raise"));
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [name](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError())
            qWarning() << "galleond: the player" << name << "cannot be brought to the front" << call->error().message();
    });
}

void MprisController::restartTrack()
{
    // Seek back further than the start: a player then goes to the start of the track (MPRIS says so).
    callPlayer(QStringLiteral("Seek"), {QVariant::fromValue<qlonglong>(-86400LL * 1000000LL)});
}

void MprisController::toggleMute()
{
    if (m_systemVolumeProgram.isEmpty())
        return;
    // The system sound, for every player alike: a fixed command, nothing from outside goes in.
    QProcess::startDetached(m_systemVolumeProgram,
                            {QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("toggle")});
}

void MprisController::seek(int seconds)
{
    // MPRIS Seek takes an offset in microseconds.
    callPlayer(QStringLiteral("Seek"), {QVariant::fromValue<qlonglong>(static_cast<qlonglong>(seconds) * 1000000LL)});
}

void MprisController::changeVolume(int percent)
{
    m_pendingVolume = std::clamp(m_pendingVolume + percent, -200, 200); // bounded: a dial cannot queue more
    if (!m_volumeBusy)
        applyPendingVolume();
}

void MprisController::applySystemVolume(int percent)
{
    if (m_systemVolumeProgram.isEmpty())
        return;
    // Steps are gathered for a moment: a fast turn starts one program, not twenty.
    m_pendingSystemVolume = std::clamp(m_pendingSystemVolume + percent, -100, 100);
    if (m_systemVolumeTimer.isActive())
        return;
    m_systemVolumeTimer.start();
}

void MprisController::applyPendingVolume()
{
    const QString name = current();
    if (name.isEmpty() && m_pendingVolume != 0) {
        // No player: the system volume, so that the dial is not dead.
        applySystemVolume(std::exchange(m_pendingVolume, 0));
        return;
    }
    if (name.isEmpty() || m_pendingVolume == 0) {
        m_pendingVolume = 0;
        return;
    }
    // Read the volume, then write it back moved: Volume is a property between
    // 0.0 and 1.0 (some players do not support it: the read then fails and
    // nothing is written).
    const int percent = std::exchange(m_pendingVolume, 0);
    m_volumeBusy = true;
    QDBusMessage get = QDBusMessage::createMethodCall(name, PlayerPath, PropertiesInterface, QStringLiteral("Get"));
    get << PlayerInterface << QStringLiteral("Volume");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get, CallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, name, percent](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *call;
        bool ok = false;
        const double volume = reply.isError() ? 0.0 : reply.value().variant().toDouble(&ok);
        if (!ok) {
            m_volumeBusy = false;
            m_pendingVolume = 0; // the player has no volume: drop what piled up
            return;
        }
        const double target = std::clamp(volume + (percent / 100.0), 0.0, 1.0);
        QDBusMessage set = QDBusMessage::createMethodCall(name, PlayerPath, PropertiesInterface, QStringLiteral("Set"));
        set << PlayerInterface << QStringLiteral("Volume") << QVariant::fromValue(QDBusVariant(target));
        auto *setWatcher = new QDBusPendingCallWatcher(m_bus.asyncCall(set, CallTimeoutMs), this);
        connect(setWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *done) {
            done->deleteLater();
            m_volumeBusy = false;
            if (m_pendingVolume != 0)
                applyPendingVolume(); // steps came in meanwhile
        });
    });
}

} // namespace galleond
