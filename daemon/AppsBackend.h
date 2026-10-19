// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "DesktopEntries.h"

#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace galleond {

// Serves the icons of the desktop's theme to the window: image://themeicon/<name>.
class ThemeIconProvider : public QQuickImageProvider
{
public:
    ThemeIconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// What the "applications on the keys" window talks to: the list of installed
// applications, the keys of a page, and the two edits (put an application on a
// key, empty a key) done through PagesEditor.
class AppsBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList apps READ apps CONSTANT)
    Q_PROPERTY(QStringList pages READ pages NOTIFY changed)
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY changed)
    Q_PROPERTY(QVariantList keys READ keys NOTIFY changed)
    Q_PROPERTY(QString file READ file CONSTANT)
    // Empty when all is well; else why the pages file cannot be edited.
    Q_PROPERTY(QString problem READ problem NOTIFY changed)

public:
    explicit AppsBackend(QString pagesFile, QObject *parent = nullptr);

    // [{name, id, icon}], by name.
    [[nodiscard]] QVariantList apps() const { return m_apps; }
    [[nodiscard]] QStringList pages() const { return m_pages; }
    [[nodiscard]] QString page() const { return m_page; }
    void setPage(const QString &page);
    // The 12 keys of the page: [{index, label, icon, kind}], kind = empty, app, media, page or back.
    [[nodiscard]] QVariantList keys() const { return m_keys; }
    [[nodiscard]] QString file() const { return m_file; }
    [[nodiscard]] QString problem() const { return m_problem; }

    // Each returns "" on success, else the reason.
    Q_INVOKABLE QString assign(const QString &desktop, int key);
    Q_INVOKABLE QString clear(int key);
    Q_INVOKABLE void reload();

Q_SIGNALS:
    void changed();

private:
    QString m_file;
    QVariantList m_apps;
    QStringList m_pages;
    QString m_page;
    QVariantList m_keys;
    QString m_problem;
};

} // namespace galleond
