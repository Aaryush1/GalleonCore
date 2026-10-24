// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "UpdateRelease.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSysInfo>
#include <QVersionNumber>

namespace bosun {
namespace {
struct Version {
    QVersionNumber number;
    QString text;
    bool prerelease = false;
};
std::optional<Version> version(const QString &text)
{
    if (text.size() > 96) return std::nullopt;
    static const QRegularExpression pattern(QStringLiteral(
        "^v?((?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*))"
        "(-[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?\\z"));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) return std::nullopt;
    const QString numeric = match.captured(1);
    qsizetype end = 0;
    const auto number = QVersionNumber::fromString(numeric, &end);
    if (end != numeric.size() || number.segmentCount() != 3) return std::nullopt;
    return Version{number, numeric, !match.captured(2).isEmpty()};
}
QString assetPattern(const UpdatePlatform &platform, const QString &releaseVersion)
{
    // Only published, known architectures/formats are offered. Unknown hosts
    // fall back to the release page instead of downloading a wrong binary.
    if (platform.architecture != QStringLiteral("x86_64")) return {};
    const auto escaped = QRegularExpression::escape(releaseVersion);
    if (platform.system == QStringLiteral("windows"))
        return QStringLiteral("^GalleonCore-%1-Windows-x86_64-Setup\\.exe\\z").arg(escaped);
    if (platform.system != QStringLiteral("linux")) return {};
    if (platform.appImage)
        return QStringLiteral("^GalleonCore-%1-x86_64\\.AppImage\\z").arg(escaped);
    if (platform.distribution == QStringLiteral("ubuntu")) {
        QString series;
        if (platform.distributionVersion == QStringLiteral("24.04")) series = QStringLiteral("noble");
        else if (platform.distributionVersion == QStringLiteral("26.04")) series = QStringLiteral("resolute");
        if (!series.isEmpty()) return QStringLiteral("^galleoncore_%1-([1-9][0-9]*)\\.%2_amd64\\.deb\\z").arg(escaped, series);
    }
    if (platform.distribution == QStringLiteral("fedora") && QRegularExpression(QStringLiteral("^[0-9]{2}\\z")).match(platform.distributionVersion).hasMatch())
        return QStringLiteral("^galleoncore-%1-([1-9][0-9]*)\\.fc%2\\.x86_64\\.rpm\\z").arg(escaped, platform.distributionVersion);
    if (platform.distribution == QStringLiteral("arch"))
        return QStringLiteral("^galleoncore-%1-([1-9][0-9]*)-x86_64\\.pkg\\.tar\\.zst\\z").arg(escaped);
    return {};
}
bool sameDownload(const QUrl &url, const QString &tag, const QString &name)
{
    return url.isValid() && url.scheme() == QStringLiteral("https") && url.host() == QStringLiteral("github.com")
        && url.userInfo().isEmpty() && url.port() == -1 && !url.hasQuery() && !url.hasFragment()
        && url.path() == QStringLiteral("/Paullux/GalleonCore/releases/download/%1/%2").arg(tag, name);
}
} // namespace

std::optional<bool> newerStableVersion(const QString &installed, const QString &candidate)
{
    const auto local = version(installed);
    const auto remote = version(candidate);
    if (!local || !remote || remote->prerelease) return std::nullopt;
    const auto comparison = QVersionNumber::compare(remote->number, local->number);
    return comparison > 0 || (comparison == 0 && local->prerelease);
}

std::optional<UpdateRelease> parseUpdateRelease(const QByteArray &json, const UpdatePlatform &platform)
{
    if (json.size() > MaxReleaseBytes) return std::nullopt;
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) return std::nullopt;
    const auto object = document.object();
    if (!object.value(QStringLiteral("draft")).isBool() || object.value(QStringLiteral("draft")).toBool()
        || !object.value(QStringLiteral("prerelease")).isBool() || object.value(QStringLiteral("prerelease")).toBool()
        || !object.value(QStringLiteral("assets")).isArray()) return std::nullopt;
    const auto tag = object.value(QStringLiteral("tag_name")).toString();
    const auto parsed = version(tag);
    if (!parsed || parsed->prerelease) return std::nullopt;
    const auto assets = object.value(QStringLiteral("assets")).toArray();
    if (assets.size() > 256) return std::nullopt;
    UpdateRelease result{tag, parsed->text, QUrl(QStringLiteral("https://github.com/Paullux/GalleonCore/releases/tag/") + tag), {}, {}};
    const auto pattern = assetPattern(platform, result.version);
    if (pattern.isEmpty()) return result;
    const QRegularExpression matcher(pattern);
    int bestRevision = -1;
    for (const auto &entry : assets) {
        const auto asset = entry.toObject();
        const auto name = asset.value(QStringLiteral("name")).toString();
        if (name.size() > 200 || asset.value(QStringLiteral("state")).toString() != QStringLiteral("uploaded")) continue;
        const auto match = matcher.match(name);
        if (!match.hasMatch()) continue;
        const auto rawUrl = asset.value(QStringLiteral("browser_download_url")).toString();
        if (rawUrl.size() > 1024) continue;
        const QUrl url(rawUrl, QUrl::StrictMode);
        if (!sameDownload(url, tag, name)) continue;
        bool ok = true;
        const int revision = match.lastCapturedIndex() == 0 ? 0 : match.captured(1).toInt(&ok);
        if (!ok || revision <= bestRevision) continue;
        bestRevision = revision;
        result.download = url;
        result.assetName = name;
    }
    return result;
}

QByteArray updateReleaseCache(const UpdateRelease &release)
{
    QJsonArray assets;
    if (!release.download.isEmpty()) assets.append(QJsonObject{
        {QStringLiteral("name"), release.assetName}, {QStringLiteral("state"), QStringLiteral("uploaded")},
        {QStringLiteral("browser_download_url"), release.download.toString()}});
    return QJsonDocument(QJsonObject{{QStringLiteral("tag_name"), release.tag}, {QStringLiteral("draft"), false},
        {QStringLiteral("prerelease"), false}, {QStringLiteral("assets"), assets}}).toJson(QJsonDocument::Compact);
}

UpdatePlatform currentUpdatePlatform()
{
#ifdef Q_OS_WIN
    const QString system = QStringLiteral("windows");
#else
    const QString system = QSysInfo::kernelType();
#endif
    return {system, QSysInfo::buildCpuArchitecture(), QSysInfo::productType(), QSysInfo::productVersion(), qEnvironmentVariableIsSet("APPIMAGE")};
}
} // namespace bosun
