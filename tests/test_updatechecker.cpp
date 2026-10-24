// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "UpdateChecker.h"
#include "Required.h"
#include "PackageUpdates.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
#include <memory>
#include <utility>

namespace {
bosun::UpdatePlatform windowsPlatform() { return {QStringLiteral("windows"), QStringLiteral("x86_64"), {}, {}, false}; }
QByteArray release(const QString &tag = QStringLiteral("v0.3.0"), const QString &asset = QStringLiteral("GalleonCore-0.3.0-Windows-x86_64-Setup.exe"), const QString &url = {})
{
    return QJsonDocument(QJsonObject{{QStringLiteral("tag_name"), tag}, {QStringLiteral("draft"), false},
        {QStringLiteral("prerelease"), false}, {QStringLiteral("assets"), QJsonArray{QJsonObject{
            {QStringLiteral("name"), asset}, {QStringLiteral("state"), QStringLiteral("uploaded")},
            {QStringLiteral("browser_download_url"), url.isEmpty() ? QStringLiteral("https://github.com/Paullux/GalleonCore/releases/download/%1/%2").arg(tag, asset) : url}}}}}).toJson();
}
class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, QByteArray body, int status, bool pending, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setRawHeader("Retry-After", "3600");
        open(QIODevice::ReadOnly);
        if (!pending) QTimer::singleShot(0, this, [this] {
            Q_EMIT readyRead();
            setFinished(true);
            Q_EMIT finished();
        });
    }
    void abort() override { setFinished(true); }
    [[nodiscard]] qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto part = m_body.mid(m_offset, maximum);
        if (part.isEmpty()) return -1;
        std::memcpy(data, part.constData(), static_cast<std::size_t>(part.size()));
        m_offset += part.size();
        return part.size();
    }
private:
    QByteArray m_body;
    qsizetype m_offset = 0;
};
class Manager final : public QNetworkAccessManager {
public:
    QByteArray body = release();
    int status = 200;
    bool pending = false;
    int requests = 0;
    QNetworkRequest last;
protected:
    QNetworkReply *createRequest(Operation /*op*/, const QNetworkRequest &request, QIODevice * /*outgoingData*/) override
    {
        ++requests;
        last = request;
        return std::make_unique<Reply>(request, body, status, pending, this).release();
    }
};
}
class TestUpdateChecker : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void panelReflectsAvailableReleaseAndDownloadAction()
    {
        const QTemporaryDir dir;
        Manager manager;
        bosun::UpdateChecker checker({QStringLiteral("0.2.0"), dir.filePath(QStringLiteral("updates.ini")), windowsPlatform()}, nullptr, &manager);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/bosun/qml/UpdatePanel.qml")));
        const std::unique_ptr<QObject> panel(component.createWithInitialProperties({{QStringLiteral("updates"), QVariant::fromValue(&checker)}}));
        QVERIFY2(panel, qPrintable(component.errorString()));
        auto *button = panel->findChild<QObject *>(QStringLiteral("downloadUpdateButton"));
        QVERIFY(button);
        QVERIFY(!button->property("visible").toBool());
        checker.checkNow();
        QTRY_VERIFY(checker.available());
        QVERIFY(button->property("visible").toBool());
        const QSignalSpy urls(&checker, &bosun::UpdateChecker::openUrlRequested);
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QCOMPARE(urls.size(), 1);
    }
    void optOutCancelsAutomaticRequest()
    {
        const QTemporaryDir dir;
        Manager manager;
        manager.pending = true;
        bosun::UpdateChecker checker({QStringLiteral("0.2.0"), dir.filePath(QStringLiteral("updates.ini")), windowsPlatform()}, nullptr, &manager);
        checker.checkAutomatically();
        QVERIFY(checker.busy());
        checker.setAutomatic(false);
        QVERIFY(!checker.busy());
        QVERIFY(!checker.automatic());
    }
    void offlinePreservesValidatedUpdate()
    {
        const QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("updates.ini"));
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("release"), release());
        settings.sync();
        Manager manager;
        manager.status = 0;
        bosun::UpdateChecker checker({QStringLiteral("0.2.0"), path, windowsPlatform()}, nullptr, &manager);
        checker.checkNow();
        QTRY_VERIFY(!checker.busy());
        QVERIFY(checker.available());
        QCOMPARE(checker.latestVersion(), QStringLiteral("0.3.0"));
    }
    void distinguishesRepositoriesFromStandalonePackages()
    {
        QVERIFY(!bosun::repositoryOffersUpdates(QStringLiteral("deb"), "galleoncore:\n Installed: 0.2.0\n *** 0.2.0 100\n 100 /var/lib/dpkg/status\n"));
        QVERIFY(bosun::repositoryOffersUpdates(QStringLiteral("deb"), " 500 https://ppa.launchpadcontent.net/paullux/galleoncore/ubuntu noble/main amd64 Packages\n 100 /var/lib/dpkg/status\n"));
        QVERIFY(!bosun::repositoryOffersUpdates(QStringLiteral("deb"), " -1 https://ppa.example/ubuntu noble/main amd64 Packages\n"));
        QVERIFY(bosun::repositoryOffersUpdates(QStringLiteral("rpm"), "GALLEON_UPDATE_REPO:copr:copr.fedorainfracloud.org:paullux:GalleonCore\n"));
        QVERIFY(!bosun::repositoryOffersUpdates(QStringLiteral("rpm"), "GALLEON_UPDATE_REPO:@System\n"));
        QVERIFY(!bosun::repositoryOffersUpdates(QStringLiteral("rpm"), "Updating repositories...\n"));
        const QTemporaryDir dir;
        Manager manager;
        bosun::UpdateChecker checker({QStringLiteral("0.2.0"), dir.filePath(QStringLiteral("updates.ini")), windowsPlatform(), 100, true}, nullptr, &manager);
        checker.setRepositoryManaged(false, true);
        QVERIFY(!checker.packageManaged());
        QVERIFY(checker.automatic());
        checker.checkAutomatically();
        QTRY_VERIFY(checker.available());
    }
    void numericVersions()
    {
        QCOMPARE(bosun::newerStableVersion(QStringLiteral("0.9.0"), QStringLiteral("v0.10.0")), std::optional<bool>(true));
        QCOMPARE(bosun::newerStableVersion(QStringLiteral("0.3.0"), QStringLiteral("v0.3.0")), std::optional<bool>(false));
        QCOMPARE(bosun::newerStableVersion(QStringLiteral("0.4.0"), QStringLiteral("v0.3.0")), std::optional<bool>(false));
        QCOMPARE(bosun::newerStableVersion(QStringLiteral("0.3.0-rc.1"), QStringLiteral("v0.3.0")), std::optional<bool>(true));
        QCOMPARE(bosun::newerStableVersion(QStringLiteral("0.3.0+local"), QStringLiteral("v0.3.0")), std::optional<bool>(false));
        for (const auto &invalid : {"v0.3.0-rc.1", "v0.3.0\n", "0.3", "0.03.0", "999999999999999.0.0"})
            QVERIFY(!bosun::newerStableVersion(QStringLiteral("0.2.0"), QString::fromLatin1(invalid)).has_value());
    }
    void trustedAssetsAndFallback()
    {
        const auto valid = bosun::parseUpdateRelease(release(), windowsPlatform());
        QVERIFY(valid);
        QVERIFY(required(valid).download.path().endsWith(QStringLiteral("-Setup.exe")));
        QCOMPARE(required(bosun::parseUpdateRelease(bosun::updateReleaseCache(required(valid)), windowsPlatform())).download, required(valid).download);
        for (const auto &url : {"http://github.com/Paullux/GalleonCore/releases/download/v0.3.0/GalleonCore-0.3.0-Windows-x86_64-Setup.exe", "https://evil.example/setup.exe", "https://github.com/another/repo/setup.exe"}) {
            const auto parsed = bosun::parseUpdateRelease(release(QStringLiteral("v0.3.0"), required(valid).assetName, QString::fromLatin1(url)), windowsPlatform());
            QVERIFY(parsed);
            QVERIFY(required(parsed).download.isEmpty());
            QCOMPARE(required(parsed).notes, QUrl(QStringLiteral("https://github.com/Paullux/GalleonCore/releases/tag/v0.3.0")));
        }
        auto arm = windowsPlatform();
        arm.architecture = QStringLiteral("arm64");
        QVERIFY(required(bosun::parseUpdateRelease(release(), arm)).download.isEmpty());
        const bosun::UpdatePlatform appImage{QStringLiteral("linux"), QStringLiteral("x86_64"), {}, {}, true};
        QVERIFY(!required(bosun::parseUpdateRelease(release(QStringLiteral("v0.3.0"), QStringLiteral("GalleonCore-0.3.0-x86_64.AppImage")), appImage)).download.isEmpty());
        QVERIFY(required(bosun::parseUpdateRelease(release(), appImage)).download.isEmpty());
    }
    void nativePackageDownloadsMatchDistribution()
    {
        const bosun::UpdatePlatform ubuntu{QStringLiteral("linux"), QStringLiteral("x86_64"), QStringLiteral("ubuntu"), QStringLiteral("24.04"), false};
        const bosun::UpdatePlatform fedora{QStringLiteral("linux"), QStringLiteral("x86_64"), QStringLiteral("fedora"), QStringLiteral("44"), false};
        const auto deb = release(QStringLiteral("v0.3.0"), QStringLiteral("galleoncore_0.3.0-1.noble_amd64.deb"));
        const auto rpm = release(QStringLiteral("v0.3.0"), QStringLiteral("galleoncore-0.3.0-1.fc44.x86_64.rpm"));
        QVERIFY(!required(bosun::parseUpdateRelease(deb, ubuntu)).download.isEmpty());
        QVERIFY(!required(bosun::parseUpdateRelease(rpm, fedora)).download.isEmpty());
        QVERIFY(required(bosun::parseUpdateRelease(deb, fedora)).download.isEmpty());
        QVERIFY(required(bosun::parseUpdateRelease(rpm, ubuntu)).download.isEmpty());
        auto unsupported = ubuntu;
        unsupported.distributionVersion = QStringLiteral("22.04");
        QVERIFY(required(bosun::parseUpdateRelease(deb, unsupported)).download.isEmpty());
    }
    void malformedReleases()
    {
        QVERIFY(!bosun::parseUpdateRelease(QByteArray("[]"), windowsPlatform()));
        QVERIFY(!bosun::parseUpdateRelease(QByteArray(bosun::MaxReleaseBytes + 1, 'x'), windowsPlatform()));
        QVERIFY(!bosun::parseUpdateRelease(release(QStringLiteral("v0.3.0-beta")), windowsPlatform()));
        auto object = QJsonDocument::fromJson(release()).object();
        object[QStringLiteral("draft")] = true;
        QVERIFY(!bosun::parseUpdateRelease(QJsonDocument(object).toJson(), windowsPlatform()));
        object[QStringLiteral("draft")] = false;
        object[QStringLiteral("prerelease")] = true;
        QVERIFY(!bosun::parseUpdateRelease(QJsonDocument(object).toJson(), windowsPlatform()));
    }
    void checksPersistAndOpenOnlyOnRequest()
    {
        const QTemporaryDir dir;
        Manager manager;
        const bosun::UpdateChecker::Options options{QStringLiteral("0.2.0"), dir.filePath(QStringLiteral("updates.ini")), windowsPlatform()};
        bosun::UpdateChecker checker(options, nullptr, &manager);
        const QSignalSpy urls(&checker, &bosun::UpdateChecker::openUrlRequested);
        QCOMPARE(manager.requests, 0);
        checker.checkAutomatically();
        checker.checkNow();
        QCOMPARE(manager.requests, 1);
        QTRY_VERIFY(!checker.busy());
        QVERIFY(checker.available());
        QVERIFY(urls.isEmpty());
        QCOMPARE(manager.last.url(), QUrl(QStringLiteral("https://api.github.com/repos/Paullux/GalleonCore/releases/latest")));
        checker.openDownload();
        QCOMPARE(urls.size(), 1);
        QVERIFY(urls.first().first().toUrl().path().endsWith(QStringLiteral("-Setup.exe")));
        checker.checkAutomatically();
        checker.checkNow();
        QCOMPARE(manager.requests, 1);
        checker.setAutomatic(false);
        bosun::UpdateChecker restored(options, nullptr, &manager);
        QVERIFY(restored.available());
        QVERIFY(!restored.automatic());
        restored.checkAutomatically();
        QCOMPARE(manager.requests, 1);
    }
    void packageManagedNeverContactsGithubOrNotifies()
    {
        const QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("updates.ini"));
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("release"), release());
        settings.setValue(QStringLiteral("automatic"), true);
        settings.sync();
        Manager manager;
        bosun::UpdateChecker checker({QStringLiteral("0.2.0"), path, windowsPlatform(), 100, true}, nullptr, &manager);
        checker.startAutomaticChecks();
        checker.checkNow();
        checker.setAutomatic(true);
        checker.checkAutomatically();
        QVERIFY(checker.packageManaged());
        QVERIFY(!checker.available());
        QVERIFY(!checker.automatic());
        QCOMPARE(manager.requests, 0);
    }
    void errorsAreBounded_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<bool>("pending");
        QTest::newRow("rate limit") << 429 << QByteArray() << false;
        QTest::newRow("offline") << 0 << QByteArray() << false;
        QTest::newRow("invalid") << 200 << QByteArray("bad json") << false;
        QTest::newRow("oversized") << 200 << QByteArray(bosun::MaxReleaseBytes + 1, 'x') << false;
        QTest::newRow("timeout") << 200 << QByteArray() << true;
    }
    void errorsAreBounded()
    {
        QFETCH(const int, status);
        QFETCH(const QByteArray, body);
        QFETCH(const bool, pending);
        const QTemporaryDir dir;
        Manager manager;
        manager.status = status;
        manager.body = body;
        manager.pending = pending;
        bosun::UpdateChecker checker({QStringLiteral("0.2.0"), dir.filePath(QStringLiteral("updates.ini")), windowsPlatform(), 20}, nullptr, &manager);
        checker.checkNow();
        QTRY_VERIFY(!checker.busy());
        QVERIFY(!checker.available());
        QVERIFY(!checker.status().isEmpty());
        checker.checkNow();
        QCOMPARE(manager.requests, 1);
    }
};
QTEST_MAIN(TestUpdateChecker)
#include "test_updatechecker.moc"
