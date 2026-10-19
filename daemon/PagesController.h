// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "MediaController.h"
#include "PagesConfig.h"
#include "TileRenderer.h"

#include <QFileSystemWatcher>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <array>

namespace galleon {
class StreamDeck;
}

namespace galleond {

// Puts the button pages on the 12 keys and acts on the keys and dials.
//
// It draws only the keys whose picture changed, so a state change of the
// player redraws one key, not twelve. A key held down is drawn lighter. The
// actions come only from the pages file: nothing here runs a program.
class PagesController : public QObject
{
    Q_OBJECT

public:
    // `deck` and `media` are not owned and must outlive the controller.
    PagesController(galleon::StreamDeck *deck, MediaController *media, TileRenderer tiles, QObject *parent = nullptr);

    // Uses this configuration and goes back to its home page.
    void setConfig(const PagesConfig &config);

    // Reads, validates and applies a pages file, then watches it: a change on
    // disk is read again. A file with errors is refused as a whole, its errors
    // are logged, and the configuration in use stays. A missing file is not an
    // error: the default pages are used until it appears.
    void loadFile(const QString &path);

    [[nodiscard]] QString currentPage() const { return m_stack.isEmpty() ? QString() : m_stack.last(); }
    [[nodiscard]] QStringList pageStack() const { return m_stack; }

    // Draws the keys; `force` sends every picture even if it did not change
    // (a module that was plugged in again shows its old icons).
    void redraw(bool force = false);

private:
    // The configuration of the key on the current page, or nullptr. Points
    // into m_config: valid until the configuration or the page changes.
    [[nodiscard]] const KeyConfig *keyConfig(int key) const;
    void onKeyPressed(int key);
    void onKeyReleased(int key);
    void onDialRotated(int dial, int steps);
    void onDialPressed(int dial);
    void perform(const Action &action, int times = 1);
    void drawKey(int key, bool force);
    void reload();
    void watch(const QString &path);

    galleon::StreamDeck *m_deck;
    MediaController *m_media;
    TileRenderer m_tiles;
    PagesConfig m_config;
    QStringList m_stack; // opened pages, the current one last
    std::array<bool, galleon::protocol::KeyCount> m_pressed{};
    std::array<QImage, galleon::protocol::KeyCount> m_sent; // what the keys show now

    QString m_file;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
};

} // namespace galleond
