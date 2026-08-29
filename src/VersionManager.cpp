#include "VersionManager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

constexpr const char *kManifestUrls[] = {
    "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json",
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json",
};

} // namespace

VersionManager::VersionManager(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void VersionManager::refresh()
{
    if (m_pendingReplies > 0)
        return; // уже грузим

    m_finished = false;
    m_lastError.clear();

    for (const char *url : kManifestUrls) {
        QNetworkRequest request(QUrl(QString::fromLatin1(url)));
        request.setTransferTimeout(10000);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("EnderForge/0.1 (Minecraft Launcher)"));
        QNetworkReply *reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply]() { handleReply(reply); });
        ++m_pendingReplies;
    }
}

bool VersionManager::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    parseManifest(file.readAll());
    m_finished = true;
    if (isLoaded()) {
        if (onVersionsLoaded)
            onVersionsLoaded();
    } else if (onLoadFailed) {
        onLoadFailed(tr("Файл манифеста не содержит версий"));
    }
    if (onRefreshFinished)
        onRefreshFinished();
    return true;
}

void VersionManager::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (m_finished) {
        // Основной ответ уже обработан — остальные игнорируем.
        --m_pendingReplies;
        return;
    }

    if (reply->error() == QNetworkReply::NoError) {
        parseManifest(reply->readAll());
        if (isLoaded()) {
            finishLoading(true);
            return;
        }
    } else if (m_lastError.isEmpty()) {
        m_lastError = reply->errorString();
    }

    if (--m_pendingReplies == 0)
        finishLoading(false);
}

void VersionManager::finishLoading(bool ok)
{
    if (m_finished)
        return;
    m_finished = true;

    if (ok) {
        if (onVersionsLoaded)
            onVersionsLoaded();
    } else if (onLoadFailed) {
        onLoadFailed(m_lastError.isEmpty()
                         ? tr("Не удалось получить список версий")
                         : tr("Не удалось получить список версий: %1").arg(m_lastError));
    }
    if (onRefreshFinished)
        onRefreshFinished();
}

void VersionManager::parseManifest(const QByteArray &data)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject root = doc.object();

    // "latest" секция
    const QJsonObject latest = root.value(QStringLiteral("latest")).toObject();
    m_latestRelease = latest.value(QStringLiteral("release")).toString();
    m_latestSnapshot = latest.value(QStringLiteral("snapshot")).toString();

    // "versions" массив
    m_versions.clear();
    const QJsonArray arr = root.value(QStringLiteral("versions")).toArray();
    m_versions.reserve(arr.size());

    for (const QJsonValue &value : arr) {
        const QJsonObject obj = value.toObject();
        VersionInfo info;
        info.id = obj.value(QStringLiteral("id")).toString();
        info.type = obj.value(QStringLiteral("type")).toString();
        info.releaseTime = QDateTime::fromString(
            obj.value(QStringLiteral("releaseTime")).toString(), Qt::ISODate);
        if (!info.id.isEmpty())
            m_versions.append(info);
    }
}
