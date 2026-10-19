// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PagesConfig.h>
#include <TileRenderer.h>

#include <QFileSystemWatcher>
#include <QObject>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <optional>

namespace bosun {

// What the window shows of the pages file: the themes (the tabs), the keys of a
// page and the picture of each key, drawn by the same code as on the module.
// Read only for now; the file is read again when it changes on disk.
class BosunBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString file READ file CONSTANT)
    // Empty when all is well; else why the pages cannot be shown.
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    // [{name, page, color}], in the order of the file; color is "#rrggbb".
    Q_PROPERTY(QVariantList themes READ themes NOTIFY changed)
    // Goes up at each reading, to make the window ask for the pictures again.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit BosunBackend(QString pagesFile, QObject *parent = nullptr);

    [[nodiscard]] QString file() const { return m_file; }
    [[nodiscard]] QString problem() const { return m_problem; }
    [[nodiscard]] QVariantList themes() const { return m_themes; }
    [[nodiscard]] int revision() const { return m_revision; }

    // The 12 keys of `page`, as written in the file: [{index, label, kind, detail, empty}].
    // Empty list for a page that does not exist.
    Q_INVOKABLE [[nodiscard]] QVariantList keys(const QString &page) const;
    // What the module adds on its own to `page` (applications by use, Steam games, menu
    // applications), in words; empty when it adds nothing.
    Q_INVOKABLE [[nodiscard]] QString autoFill(const QString &page) const;
    Q_INVOKABLE void reload();

    // The 160 x 160 picture of key `key` of `page`; a blank key when there is none.
    [[nodiscard]] QPixmap tile(const QString &page, int key) const;

Q_SIGNALS:
    void changed();

private:
    void watchFile();
    [[nodiscard]] const galleond::Page *pageNamed(const QString &page) const;

    QString m_file;
    QString m_problem;
    QVariantList m_themes;
    int m_revision = 0;
    std::optional<galleond::PagesConfig> m_config;
    galleond::TileRenderer m_tiles;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadSoon; // a save often arrives as several changes
};

// Serves image://tiles/<page>/<key>; a "?..." after the key is ignored (it
// is there to make the window ask again after a change).
class TileProvider : public QQuickImageProvider
{
public:
    explicit TileProvider(const BosunBackend *backend);
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    const BosunBackend *m_backend;
};

} // namespace bosun
