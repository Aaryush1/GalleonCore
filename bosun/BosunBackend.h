// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PagesConfig.h>
#include "VideoConverter.h"

#include <PagesEditor.h>
#include <TileRenderer.h>
#include <VideoProbe.h>
#include <VideoThumbnails.h>

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

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
    // The names of all the pages (the pages of the themes, and the sub-pages they lead to).
    Q_PROPERTY(QStringList pageNames READ pageNames NOTIFY changed)
    // What a key can do: [{id, label}] for media, the names of the keys of the keypad.
    Q_PROPERTY(QVariantList mediaCommands READ mediaCommands CONSTANT)
    Q_PROPERTY(QStringList keypadKeys READ keypadKeys CONSTANT)
    // Goes up at each reading, to make the window ask for the pictures again.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // The screensaver of the file, with what is known of each video (see screensaver()).
    Q_PROPERTY(QVariantMap screensaver READ screensaver NOTIFY changed)
    // The video being looked at before it is added (see inspect()); empty map when none.
    Q_PROPERTY(QVariantMap candidate READ candidate NOTIFY changed)
    // The conversion of the video being looked at: {state, fraction, message}; `state` is
    // "idle", "running", "done", "failed" or "cancelled".
    Q_PROPERTY(QVariantMap conversion READ conversion NOTIFY changed)

public:
    // The user's own folder for converted videos: ~/.local/share/galleoncore/screensavers.
    [[nodiscard]] static QString defaultConvertedFolder();

    // The programs and the folder the screensaver tab uses; the defaults are the ones of galleond.
    struct Tools
    {
        QString ffprobe = QStringLiteral("ffprobe");
        QString ffmpeg = QStringLiteral("ffmpeg");
        QString thumbnailFolder; // empty: the user's cache folder, shared with galleond
        QString convertedFolder = defaultConvertedFolder(); // where the results are written
    };

    explicit BosunBackend(QString pagesFile, QObject *parent = nullptr);
    BosunBackend(QString pagesFile, const Tools &tools, QObject *parent = nullptr);

    [[nodiscard]] QString file() const { return m_file; }
    [[nodiscard]] QString problem() const { return m_problem; }
    [[nodiscard]] QVariantList themes() const { return m_themes; }
    [[nodiscard]] QStringList pageNames() const;
    [[nodiscard]] static QVariantList mediaCommands();
    [[nodiscard]] static QStringList keypadKeys();
    // The installed applications, [{name, id, icon}] by name (read from the menu on the first call).
    Q_INVOKABLE [[nodiscard]] QVariantList installedApps();
    [[nodiscard]] int revision() const { return m_revision; }

    // {enabled, hasGeometry, idleMinutes, fps, whilePlaying, probeMissing, canvas, videos}. `probeMissing`: ffprobe was not found,
    // so no size or rate is known. `canvas` is
    // the size a video is cut to ("720 x 1245"), empty without geometry. `videos` is
    // [{index, title, file, name, found, details, thumbnail, notes}]: `details` is the size,
    // rate and length once ffprobe has answered, `thumbnail` a file URL once there is one,
    // `notes` the words of what will not be quite right (cut, enlarged, no file).
    [[nodiscard]] QVariantMap screensaver() const { return m_screensaver; }

    // {path, name, state, details, notes, thumbnail, canAdd, why, suggestedTitle}. `state` is
    // "checking" (ffprobe has not answered), "ready" or "unusable"; `why` says why it cannot be
    // added (not a video, list full, already in the list); `notes` as for a video of the list.
    [[nodiscard]] QVariantMap candidate() const { return m_candidate; }
    [[nodiscard]] QVariantMap conversion() const { return m_conversion; }

    // The 12 keys of `page`, as written in the file: [{index, label, kind, detail, empty}], and
    // for an editor what is configured: type, command, step, seconds, pageTarget, desktop,
    // keyName, appid, rawLabel, rawIcon.
    // Empty list for a page that does not exist.
    Q_INVOKABLE [[nodiscard]] QVariantList keys(const QString &page) const;
    // What the module adds on its own to `page` (applications by use, Steam games, menu
    // applications), in words; empty when it adds nothing.
    Q_INVOKABLE [[nodiscard]] QString autoFill(const QString &page) const;
    Q_INVOKABLE void reload();

    // The edits of the keys and of the tabs. Each returns "" when it is done (the file is written
    // with a backup and read again at once), else why it was refused; nothing is written then.
    // `spec` holds: type, desktop, command, step, seconds, page, keyName, label, icon (see KeySpec).
    Q_INVOKABLE [[nodiscard]] QString setKey(const QString &page, int key, const QVariantMap &spec);
    Q_INVOKABLE [[nodiscard]] QString clearKey(const QString &page, int key);
    Q_INVOKABLE [[nodiscard]] QString addTheme(const QString &name, const QString &color);
    Q_INVOKABLE [[nodiscard]] QString setTheme(int index, const QString &name, const QString &color);
    Q_INVOKABLE [[nodiscard]] QString removeTheme(int index);
    Q_INVOKABLE [[nodiscard]] QString moveTheme(int index, int delta);

    // The edits of the screensaver tab. Each returns "" when it is done (the file is written
    // with a backup and read again at once), else why it was refused; nothing is written then.
    Q_INVOKABLE [[nodiscard]] QString setIdleMinutes(int minutes);
    Q_INVOKABLE [[nodiscard]] QString setFps(int fps);
    Q_INVOKABLE [[nodiscard]] QString setWhilePlaying(bool whilePlaying);
    Q_INVOKABLE [[nodiscard]] QString setVideoTitle(int index, const QString &title);
    Q_INVOKABLE [[nodiscard]] QString removeVideo(int index);

    // Looks at a video the user chose (a path or a file:// address) without changing anything.
    Q_INVOKABLE void inspect(const QString &pathOrUrl);
    // Adds the video being looked at to the list, as it is; "" when done, else why not.
    Q_INVOKABLE [[nodiscard]] QString addCandidate(const QString &title);
    Q_INVOKABLE void clearCandidate();

    // The crop rectangle of the video being looked at ({x, y, width, height}, in its pixels): the
    // largest centred one, then moved, zoomed (`factor` above 1: closer, around the point
    // (anchorX, anchorY) of the video) or made valid. Without a video they give the rectangle back.
    Q_INVOKABLE [[nodiscard]] QVariantMap cropCentered() const;
    Q_INVOKABLE [[nodiscard]] QVariantMap cropMoved(const QVariantMap &crop, int dx, int dy) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap cropZoomed(const QVariantMap &crop, double factor, double anchorX, double anchorY) const;

    // The same for any picture: `source` is its size and `canvas` the size of what the crop is for.
    Q_INVOKABLE [[nodiscard]] static QVariantMap cropCenteredFor(int sourceWidth, int sourceHeight, int canvasWidth, int canvasHeight);
    Q_INVOKABLE [[nodiscard]] static QVariantMap cropMovedFor(const QVariantMap &crop, int dx, int dy, int sourceWidth, int sourceHeight,
                                                              int canvasWidth, int canvasHeight);
    Q_INVOKABLE [[nodiscard]] static QVariantMap cropZoomedFor(const QVariantMap &crop, double factor, double anchorX, double anchorY,
                                                               int sourceWidth, int sourceHeight, int canvasWidth, int canvasHeight);

    // Cuts the video being looked at to the picture of the module, keeping the part
    // (x, y, width, height) of it (in the pixels of the video; made valid first), writes the
    // result in the user's screensaver folder and adds it to the list when FFmpeg is done.
    // "" when it started, else why not. The original file is never touched.
    Q_INVOKABLE [[nodiscard]] QString startConversion(int x, int y, int width, int height, const QString &title);
    Q_INVOKABLE void cancelConversion();

    // The 160 x 160 picture of key `key` of `page`; a blank key when there is none. The keys are
    // tinted with the colour of the theme of `themePage` (else of `page`): a sub-page takes the
    // colour of the tab it is shown in.
    [[nodiscard]] QPixmap tile(const QString &page, int key, const QString &themePage = {}) const;

Q_SIGNALS:
    void changed();

private:
    void watchFile();
    [[nodiscard]] QString applied(const galleond::EditResult &result);
    void buildScreensaver();
    void buildCandidate();
    // The sizes the crop works with: the video being looked at and the picture of the module (empty when unknown).
    [[nodiscard]] QSize candidateSource() const;
    [[nodiscard]] QSize canvas() const;
    void setConversion(const QString &state, double fraction = 0, const QString &message = {});
    void conversionDone(const QString &outputFile);
    void askAboutVideos();
    void nextThumbnail();
    [[nodiscard]] const galleond::Page *pageNamed(const QString &page) const;

    QString m_file;
    QString m_problem;
    QVariantList m_themes;
    QVariantList m_apps;
    bool m_appsRead = false;
    int m_revision = 0;
    std::optional<galleond::PagesConfig> m_config;
    galleond::TileRenderer m_tiles;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadSoon; // a save often arrives as several changes
    galleond::VideoProbe m_probe;
    galleond::VideoThumbnails m_thumbnails;
    QVariantMap m_screensaver;
    QString m_candidatePath; // resolved; empty when nothing is being looked at
    QString m_candidateProblem; // why what was chosen is not a usable file, when it is not
    QString m_candidateChosen;  // what was chosen, as the user wrote it
    QVariantMap m_candidate;
    QVariantMap m_conversion{{QStringLiteral("state"), QStringLiteral("idle")}, {QStringLiteral("fraction"), 0.0}, {QStringLiteral("message"), QString()}};
    VideoConverter m_converter;
    QString m_convertedFolder;
    QString m_conversionTitle;
    QStringList m_videoFiles;                         // the resolved path of each video, "" when it is not readable
    QHash<QString, galleond::VideoInfo> m_videoInfo;   // by path
    QHash<QString, QString> m_videoPictures;           // by path
    QStringList m_pictureTried;                        // no picture is asked for twice
    bool m_pictureBusy = false;                        // a picture is being made
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
