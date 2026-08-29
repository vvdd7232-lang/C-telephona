#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;

// Скачивание клиента Minecraft для профиля.
//
// Шаги:
//   1. Получить JSON-описание версии (versionJsonUrl из манифеста Mojang)
//   2. Извлечь downloads.client.url
//   3. Скачать jar-файл клиента в destPath (с прогрессом)
class GameDownloader : public QObject
{
public:
    explicit GameDownloader(QObject *parent = nullptr);

    // Колбэки (аналог сигналов)
    std::function<void(qint64 received, qint64 total)> onProgress;
    std::function<void(bool ok, const QString &error)> onFinished;

    // Начать скачивание. destPath — полный путь к jar-файлу.
    void downloadClient(const QString &versionJsonUrl, const QString &destPath);

    bool isBusy() const { return m_busy; }

private:
    void fetchVersionJson();
    void startJarDownload(const QString &url, qint64 expectedSize);
    void fail(const QString &error);

    QNetworkAccessManager *m_network = nullptr;
    bool m_busy = false;
    QString m_versionJsonUrl;
    QString m_destPath;
};
