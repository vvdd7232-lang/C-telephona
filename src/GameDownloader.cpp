#include "GameDownloader.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

GameDownloader::GameDownloader(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void GameDownloader::downloadClient(const QString &versionJsonUrl, const QString &destPath)
{
    if (m_busy)
        return;
    if (versionJsonUrl.isEmpty() || destPath.isEmpty()) {
        fail(QStringLiteral("Некорректные параметры скачивания"));
        return;
    }
    m_busy = true;
    m_versionJsonUrl = versionJsonUrl;
    m_destPath = destPath;
    fetchVersionJson();
}

void GameDownloader::fetchVersionJson()
{
    QNetworkRequest request{QUrl(m_versionJsonUrl)};
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EnderForge/0.2 (Minecraft Launcher)"));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("Не удалось получить описание версии: %1")
                     .arg(reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject client = doc.object()
                                      .value(QStringLiteral("downloads"))
                                      .toObject()
                                      .value(QStringLiteral("client"))
                                      .toObject();
        const QString jarUrl = client.value(QStringLiteral("url")).toString();
        if (jarUrl.isEmpty()) {
            fail(QStringLiteral("В описании версии нет ссылки на клиент"));
            return;
        }
        const qint64 size = client.value(QStringLiteral("size")).toDouble(0);
        startJarDownload(jarUrl, size);
    });
}

void GameDownloader::startJarDownload(const QString &url, qint64 expectedSize)
{
    QDir().mkpath(QFileInfo(m_destPath).absolutePath());

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(60000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EnderForge/0.2 (Minecraft Launcher)"));

    auto *save = new QSaveFile(m_destPath, this);
    if (!save->open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("Не удалось создать файл %1").arg(m_destPath));
        return;
    }

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (onProgress)
            onProgress(got, total);
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply, save]() {
        save->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, save, expectedSize]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            save->cancelWriting();
            save->deleteLater();
            fail(QStringLiteral("Ошибка скачивания: %1").arg(reply->errorString()));
            return;
        }
        const bool sizeOk = expectedSize <= 0 || reply->bytesAvailable() <= 0
            || save->size() >= expectedSize;
        if (!save->commit() || !sizeOk) {
            save->deleteLater();
            fail(QStringLiteral("Файл клиента записан некорректно"));
            return;
        }
        save->deleteLater();
        m_busy = false;
        if (onFinished)
            onFinished(true, QString());
    });
}

void GameDownloader::fail(const QString &error)
{
    m_busy = false;
    if (onFinished)
        onFinished(false, error);
}
