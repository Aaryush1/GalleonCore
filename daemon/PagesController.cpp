// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PagesController.h"

#include <galleon/StreamDeck.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace galleond {

namespace {

constexpr int ReloadDelayMs = 250; // editors write a file in several steps

} // namespace

PagesController::PagesController(galleon::StreamDeck *deck, MediaController *media, TileRenderer tiles, QObject *parent)
    : QObject(parent)
    , m_deck(deck)
    , m_media(media)
    , m_tiles(std::move(tiles))
{
    // The built-in pages: always valid, so there is always something to show.
    if (const auto fallback = parsePagesConfig(defaultPagesJson()); fallback.config)
        setConfig(*fallback.config);

    connect(m_deck, &galleon::StreamDeck::keyPressed, this, &PagesController::onKeyPressed);
    connect(m_deck, &galleon::StreamDeck::keyReleased, this, &PagesController::onKeyReleased);
    connect(m_deck, &galleon::StreamDeck::dialRotated, this, &PagesController::onDialRotated);
    connect(m_deck, &galleon::StreamDeck::dialPressed, this, &PagesController::onDialPressed);
    connect(m_media, &MediaController::stateChanged, this, [this] { redraw(); });

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(ReloadDelayMs);
    connect(&m_reloadTimer, &QTimer::timeout, this, &PagesController::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
}

void PagesController::setConfig(const PagesConfig &config)
{
    m_config = config;
    m_stack = {config.home};
    m_pressed.fill(false);
    redraw(true);
}

void PagesController::loadFile(const QString &path)
{
    m_file = path;
    reload();
}

void PagesController::watch(const QString &path)
{
    // A file that an editor replaces drops out of the watch list: add it again
    // after each change, and watch its folder to see it appear.
    const QFileInfo info(path);
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (info.exists() && !watched.contains(info.absoluteFilePath()))
        m_watcher.addPath(info.absoluteFilePath());
    if (QDir(info.absolutePath()).exists() && !watched.contains(info.absolutePath()))
        m_watcher.addPath(info.absolutePath());
}

void PagesController::reload()
{
    if (m_file.isEmpty())
        return;
    watch(m_file);

    QFile file(m_file);
    if (!file.exists()) {
        qInfo() << "galleond: no pages file at" << m_file << "- the built-in pages are used";
        return;
    }
    // Read one byte more than the limit: parsePagesConfig refuses a longer file.
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "galleond: cannot read the pages file" << m_file << file.errorString();
        return;
    }
    const QByteArray bytes = file.read(MaxPagesFileBytes + 1);
    const PagesParseResult result = parsePagesConfig(bytes);
    if (!result.config) {
        qWarning() << "galleond: the pages file" << m_file << "has errors and is not used:";
        for (const QString &line : result.errors)
            qWarning().noquote() << "   " << line;
        return;
    }
    if (*result.config == m_config)
        return;
    qInfo() << "galleond: pages loaded from" << m_file;
    setConfig(*result.config);
}

const KeyConfig *PagesController::keyConfig(int key) const
{
    const auto page = m_config.pages.constFind(currentPage());
    if (page == m_config.pages.constEnd() || key < 0 || key >= galleon::protocol::KeyCount)
        return nullptr;
    const auto &configured = page->keys[static_cast<std::size_t>(key)];
    return configured ? &*configured : nullptr;
}

void PagesController::drawKey(int key, bool force)
{
    if (!m_deck->isOpen())
        return;
    const auto index = static_cast<std::size_t>(key);
    const KeyConfig *configured = keyConfig(key);

    QImage image;
    if (configured)
        image = m_tiles.render(*configured, MediaController::stateName(m_media->state()), m_pressed[index]);
    else
        image = m_tiles.blank();

    if (!force && image == m_sent[index])
        return;
    // A failed send is forgotten, so that the next redraw tries again.
    m_sent[index] = m_deck->setKeyImage(key, image) ? image : QImage();
}

void PagesController::redraw(bool force)
{
    for (int key = 0; key < galleon::protocol::KeyCount; ++key)
        drawKey(key, force);
}

void PagesController::perform(const Action &action, int times)
{
    switch (action.type) {
    case ActionType::None:
        return;
    case ActionType::Page:
        if (m_config.pages.contains(action.page) && m_stack.size() < MaxPageDepth) {
            m_stack.append(action.page);
            m_pressed.fill(false);
            redraw();
        }
        return;
    case ActionType::Back:
        if (m_stack.size() > 1) {
            m_stack.removeLast();
            m_pressed.fill(false);
            redraw();
        }
        return;
    case ActionType::Media:
        break;
    }

    switch (action.command) {
    case MediaCommand::PlayPause:
        m_media->playPause();
        break;
    case MediaCommand::Next:
        m_media->next();
        break;
    case MediaCommand::Previous:
        m_media->previous();
        break;
    case MediaCommand::Stop:
        m_media->stop();
        break;
    case MediaCommand::Volume:
        m_media->changeVolume(action.step * times);
        break;
    case MediaCommand::Seek:
        m_media->seek(action.seconds * times);
        break;
    }
}

void PagesController::onKeyPressed(int key)
{
    if (key < 0 || key >= galleon::protocol::KeyCount)
        return;
    const KeyConfig *configured = keyConfig(key);
    if (!configured)
        return;
    const Action action = configured->action; // a copy: performing it may change the page
    m_pressed[static_cast<std::size_t>(key)] = true;
    drawKey(key, false);
    perform(action); // may change the page: the pressed flags are reset there
}

void PagesController::onKeyReleased(int key)
{
    if (key < 0 || key >= galleon::protocol::KeyCount || !m_pressed[static_cast<std::size_t>(key)])
        return;
    m_pressed[static_cast<std::size_t>(key)] = false;
    drawKey(key, false);
}

void PagesController::onDialRotated(int dial, int steps)
{
    if (dial < 0 || dial >= galleon::protocol::DialCount || steps == 0)
        return;
    perform(m_config.dials[static_cast<std::size_t>(dial)].turn, steps);
}

void PagesController::onDialPressed(int dial)
{
    if (dial < 0 || dial >= galleon::protocol::DialCount)
        return;
    perform(m_config.dials[static_cast<std::size_t>(dial)].press);
}

} // namespace galleond
