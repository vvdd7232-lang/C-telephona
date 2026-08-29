#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Одна версия Minecraft из манифеста Mojang.
struct VersionInfo {
    QString id;             // например "26.2" или "1.21.4"
    QString type;           // "release" | "snapshot" | "old_beta" | "old_alpha"
    QDateTime releaseTime;  // дата релиза
};

// Загрузка и парсинг списка версий Minecraft.
//
// Источник — официальный манифест Mojang:
//   https://launchermeta.mojang.com/mc/game/version_manifest_v2.json
//   https://piston-meta.mojang.com/mc/game/version_manifest_v2.json (запасной)
//
// Плюс поддержка локального файла (--manifest) для офлайн-режима и тестов.
//
// Примечание: вместо сигналов Q_OBJECT здесь используются std::function-колбэки,
// чтобы собираться без moc (в песочнице его нет). Обычные Qt-сигналы
// (QNetworkReply::finished и т.п.) работают как обычно.
class VersionManager : public QObject
{
public:
    explicit VersionManager(QObject *parent = nullptr);

    // Последние версии из манифеста ("latest" секция)
    QString latestRelease() const { return m_latestRelease; }
    QString latestSnapshot() const { return m_latestSnapshot; }

    // Все версии (в порядке манифеста: новые сверху)
    QVector<VersionInfo> versions() const { return m_versions; }

    bool isLoaded() const { return !m_versions.isEmpty(); }

    // Колбэки (аналог сигналов)
    std::function<void()> onVersionsLoaded;
    std::function<void(const QString &)> onLoadFailed;
    std::function<void()> onRefreshFinished;

public slots:
    // Загрузить из официального манифеста (по сети)
    void refresh();

    // Загрузить из локального файла (офлайн / тесты)
    bool loadFromFile(const QString &path);

private:
    void parseManifest(const QByteArray &data);
    void handleReply(QNetworkReply *reply);
    void finishLoading(bool ok);

    QNetworkAccessManager *m_network = nullptr;
    QVector<VersionInfo> m_versions;
    QString m_latestRelease;
    QString m_latestSnapshot;
    int m_pendingReplies = 0;
    bool m_finished = false;
    QString m_lastError;
};
