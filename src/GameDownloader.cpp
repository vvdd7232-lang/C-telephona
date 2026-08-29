#include "GameDownloader.h"
#include "NetUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    request.setTransferTimeout(30000);
    request.setMaximumRedirectsAllowed(10);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EnderForge/0.3 (Minecraft Launcher)"));
    request.setRawHeader("Accept", "*/*");
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("Не удалось получить описание версии: %1")
                     .arg(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        if (isBadDownloadPayload(data)) {
            fail(QStringLiteral("Не удалось получить описание версии: %1")
                     .arg(describeBadPayload(data)));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        const QJsonObject client = doc.object()
                                      .value(QStringLiteral("downloads"))
                                      .toObject()
                                      .value(QStringLiteral("client"))
                                      .toObject();
        const QString jarUrl = client.value(QStringLiteral("url")).toString();
        if (jarUrl.isEmpty()) {
            fail(QStringLiteral("В описании версии нет ссылки на клиент: %1")
                     .arg(describeBadPayload(data)));
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
    request.setTransferTimeout(300000);
    request.setMaximumRedirectsAllowed(10);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EnderForge/0.3 (Minecraft Launcher)"));
    request.setRawHeader("Accept", "*/*");

    auto *save = new QSaveFile(m_destPath, this);
    if (!save->open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("Не удалось создать файл %1").arg(m_destPath));
        return;
    }

    auto *head = new QByteArray;
    auto *written = new qint64(0);
    auto writeChunk = [save, head, written](const QByteArray &chunk) {
        if (chunk.isEmpty())
            return;
        if (head->size() < 512)
            head->append(chunk.left(512 - head->size()));
        save->write(chunk);
        *written += chunk.size();
    };

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (onProgress)
            onProgress(got, total);
    });
    connect(reply, &QNetworkReply::readyRead, this, [reply, writeChunk]() {
        writeChunk(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, save, expectedSize, writeChunk, head, written]() {
        writeChunk(reply->readAll());
        reply->deleteLater();

        const QByteArray preview = *head;
        const qint64 n = *written;
        delete head;
        delete written;

        if (reply->error() != QNetworkReply::NoError) {
            save->cancelWriting();
            save->deleteLater();
            fail(QStringLiteral("Ошибка скачивания: %1").arg(reply->errorString()));
            return;
        }
        if (n <= 0 || payloadLooksLikeHtml(preview)) {
            save->cancelWriting();
            save->deleteLater();
            fail(describeBadPayload(preview));
            return;
        }
        if (expectedSize > 0 && n < expectedSize) {
            save->cancelWriting();
            save->deleteLater();
            fail(QStringLiteral("Файл обрезан: %1 из %2 байт").arg(n).arg(expectedSize));
            return;
        }
        if (!save->commit()) {
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
