#pragma once

#include <QObject>
#include <QString>
#include <QVector>

// Профиль запуска — аналог «игры» в Steam: версия + загрузчик,
// клиент либо скачан, либо профиль просто сохранён.
struct GameProfile {
    QString name;           // название профиля ("Выживание", "Моды"…)
    QString versionId;      // версия Minecraft ("26.2", "1.21.4"…)
    QString loader;         // vanilla | fabric | forge | neoforge | quilt
    bool downloaded = false;
    QString downloadError;  // текст ошибки, если скачивание не удалось
    QString createdAt;      // ISO-дата создания
    QString clientPath;     // путь к jar-файлу клиента (пусто, если не скачан)
};

QString loaderDisplayName(const QString &loader);
QStringList loaderIds();
QString statusLabel(const GameProfile &profile);
QColor statusColor(const GameProfile &profile);

// Хранилище профилей (profiles.json в каталоге данных).
class ProfileStore : public QObject
{
public:
    explicit ProfileStore(const QString &dataDir, QObject *parent = nullptr);

    QVector<GameProfile> profiles() const { return m_profiles; }

    // Добавить профиль и сохранить
    void addProfile(const GameProfile &p);
    // Обновить профиль по имени (возвращает true, если найден) и сохранить
    bool updateProfile(const GameProfile &p);
    bool load();
    bool save();

    QString dataDir() const { return m_dataDir; }
    QString clientDirFor(const GameProfile &p) const;
    QString clientPathFor(const GameProfile &p) const;

private:
    static QString sanitizeName(const QString &name);

    QString m_dataDir;
    QVector<GameProfile> m_profiles;
};
