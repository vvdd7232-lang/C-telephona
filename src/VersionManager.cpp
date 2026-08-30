#include "VersionManager.h"
#include "NetUtil.h"

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
    const int generation = ++m_generation;

    for (const char *url : kManifestUrls) {
        QNetworkRequest request(QUrl(QString::fromLatin1(url)));
        request.setTransferTimeout(15000);
        request.setMaximumRedirectsAllowed(10);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("EnderForge/0.5 (Minecraft Launcher)"));
        request.setRawHeader("Accept", "application/json");
        QNetworkReply *reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, generation]() { handleReply(reply, generation); });
        ++m_pendingReplies;
    }
}

bool VersionManager::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    // Отменяем любой сетевой запрос: локальный манифест имеет приоритет.
    ++m_generation;
    m_pendingReplies = 0;
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

void VersionManager::handleReply(QNetworkReply *reply, int generation)
{
    reply->deleteLater();

    if (generation != m_generation || m_finished) {
        // Ответ от предыдущего refresh() или после загрузки из файла — игнорируем.
        return;
    }

    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        if (isBadDownloadPayload(data)) {
            if (m_lastError.isEmpty())
                m_lastError = describeBadPayload(data);
        } else {
            parseManifest(data);
            if (isLoaded()) {
                m_pendingReplies = 0;
                finishLoading(true);
                return;
            }
            if (m_lastError.isEmpty())
                m_lastError = describeBadPayload(data);
        }
    } else if (m_lastError.isEmpty()) {
        m_lastError = reply->errorString();
    }

    if (--m_pendingReplies <= 0)
        finishLoading(false);
}

void VersionManager::finishLoading(bool ok)
{
    if (m_finished)
        return;
    m_finished = true;
    m_pendingReplies = 0;

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
        info.url = obj.value(QStringLiteral("url")).toString();
        if (!info.id.isEmpty())
            m_versions.append(info);
    }
}

QString VersionManager::versionJsonUrl(const QString &id) const
{
    for (const VersionInfo &v : m_versions) {
        if (v.id == id)
            return v.url;
    }
    return QString();
}
