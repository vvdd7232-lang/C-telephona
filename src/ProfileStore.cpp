#include "ProfileStore.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

constexpr const char *kProfilesFile = "profiles.json";

} // namespace

QString loaderDisplayName(const QString &loader)
{
    if (loader == QLatin1String("fabric"))    return QStringLiteral("Fabric");
    if (loader == QLatin1String("forge"))     return QStringLiteral("Forge");
    if (loader == QLatin1String("neoforge"))  return QStringLiteral("NeoForge");
    if (loader == QLatin1String("quilt"))     return QStringLiteral("Quilt");
    return QStringLiteral("Vanilla");
}

QStringList loaderIds()
{
    return { QStringLiteral("vanilla"), QStringLiteral("fabric"),
             QStringLiteral("forge"), QStringLiteral("neoforge"), QStringLiteral("quilt") };
}

QString statusLabel(const GameProfile &p)
{
    if (p.downloaded)
        return QStringLiteral("клиент скачан");
    if (!p.downloadError.isEmpty())
        return QStringLiteral("ошибка: %1").arg(p.downloadError);
    return QStringLiteral("клиент не скачан");
}

QColor statusColor(const GameProfile &p)
{
    if (p.downloaded)
        return QColor(0x3d, 0xdc, 0x68);                 // зелёный
    if (!p.downloadError.isEmpty())
        return QColor(0xe0, 0x5c, 0x5c);                 // красный
    return QColor(0x8b, 0x98, 0xa8);                     // серый
}

ProfileStore::ProfileStore(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
}

void ProfileStore::addProfile(const GameProfile &p)
{
    m_profiles.append(p);
    save();
}

bool ProfileStore::updateProfile(const GameProfile &p)
{
    for (GameProfile &existing : m_profiles) {
        if (existing.name == p.name) {
            existing = p;
            save();
            return true;
        }
    }
    return false;
}

bool ProfileStore::load()
{
    m_profiles.clear();
    QFile file(QDir(m_dataDir).filePath(QString::fromLatin1(kProfilesFile)));
    if (!file.exists())
        return true; // ещё нет профилей — не ошибка

    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return false;

    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        GameProfile p;
        p.name = obj.value(QStringLiteral("name")).toString();
        p.versionId = obj.value(QStringLiteral("version")).toString();
        p.loader = obj.value(QStringLiteral("loader")).toString();
        p.downloaded = obj.value(QStringLiteral("downloaded")).toBool();
        p.downloadError = obj.value(QStringLiteral("error")).toString();
        p.createdAt = obj.value(QStringLiteral("createdAt")).toString();
        p.clientPath = obj.value(QStringLiteral("clientPath")).toString();
        if (!p.name.isEmpty())
            m_profiles.append(p);
    }
    return true;
}

bool ProfileStore::save()
{
    QDir().mkpath(m_dataDir);

    QJsonArray arr;
    for (const GameProfile &p : m_profiles) {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), p.name);
        obj.insert(QStringLiteral("version"), p.versionId);
        obj.insert(QStringLiteral("loader"), p.loader);
        obj.insert(QStringLiteral("downloaded"), p.downloaded);
        obj.insert(QStringLiteral("error"), p.downloadError);
        obj.insert(QStringLiteral("createdAt"), p.createdAt);
        obj.insert(QStringLiteral("clientPath"), p.clientPath);
        arr.append(obj);
    }

    QFile file(QDir(m_dataDir).filePath(QString::fromLatin1(kProfilesFile)));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

QString ProfileStore::clientDirFor(const GameProfile &p) const
{
    return QDir(m_dataDir).filePath(QStringLiteral("clients") + QLatin1Char('/') + sanitizeName(p.name));
}

QString ProfileStore::clientPathFor(const GameProfile &p) const
{
    return QDir(clientDirFor(p)).filePath(p.versionId + QStringLiteral(".jar"));
}

QString ProfileStore::sanitizeName(const QString &name)
{
    QString out;
    for (const QChar &c : name) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
            out.append(c);
        else
            out.append(QLatin1Char('_'));
    }
    return out.isEmpty() ? QStringLiteral("profile") : out;
}
