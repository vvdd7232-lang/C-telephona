#include "MinecraftInstaller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>

namespace {

constexpr const char *kAssetsUrl = "https://resources.download.minecraft.net/";
constexpr const char *kFabricMetaUrl = "https://meta.fabricmc.net/v2/versions/loader/";
constexpr const char *kFabricMavenUrl = "https://maven.fabricmc.net/";
constexpr const char *kQuiltMetaUrl = "https://meta.quiltmc.org/v3/versions/loader/";
constexpr const char *kQuiltMavenUrl = "https://maven.quiltmc.org/repository/release/";
constexpr const char *kJreUrl =
    "https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jre/hotspot/normal/eclipse";

// Путь библиотеки из Maven-координат: group:artifact:version[:classifier]
QString mavenPathFromName(const QString &name, const QString &classifier)
{
    const QStringList parts = name.split(QLatin1Char(':'));
    if (parts.size() < 3)
        return QString();
    const QString group = parts.at(0);
    const QString artifact = parts.at(1);
    const QString version = parts.at(2);
    QString file = artifact + QLatin1Char('-') + version;
    if (!classifier.isEmpty())
        file += QLatin1Char('-') + classifier;
    file += QStringLiteral(".jar");
    QString path = group;
    path.replace(QLatin1Char('.'), QLatin1Char('/'));
    return path + QLatin1Char('/') + artifact + QLatin1Char('/') + version + QLatin1Char('/') + file;
}

// Применяются ли правила библиотеки на windows x64 (по умолчанию — да).
bool libraryAllowed(const QJsonObject &lib)
{
    const QJsonArray rules = lib.value(QStringLiteral("rules")).toArray();
    if (rules.isEmpty())
        return true;

    bool allowed = false;
    for (const QJsonValue &rv : rules) {
        const QJsonObject rule = rv.toObject();
        const QString action = rule.value(QStringLiteral("action")).toString();
        bool match = true;

        const QJsonObject os = rule.value(QStringLiteral("os")).toObject();
        if (!os.isEmpty()) {
            const QString name = os.value(QStringLiteral("name")).toString();
            if (!name.isEmpty() && name != QLatin1String("windows"))
                match = false;
            const QString arch = os.value(QStringLiteral("arch")).toString();
            if (match && !arch.isEmpty()) {
#ifdef _WIN64
                if (arch == QLatin1String("x86"))
                    match = false; // 32-битные библиотеки на x64 не нужны
#else
                if (arch == QLatin1String("x64"))
                    match = false;
#endif
            }
        }
        if (match)
            allowed = (action == QLatin1String("allow"));
    }
    return allowed;
}

QString fromJson(const QJsonObject &o, const char *key)
{
    return o.value(QString::fromLatin1(key)).toString();
}

} // namespace

MinecraftInstaller::MinecraftInstaller(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_gameProcess(new QProcess(this))
{
    // Захват вывода игры (для лога/статуса)
    connect(m_gameProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        if (onGameOutput)
            onGameOutput(QString::fromUtf8(m_gameProcess->readAllStandardOutput()));
    });
    connect(m_gameProcess, &QProcess::readyReadStandardError, this, [this]() {
        if (onGameOutput)
            onGameOutput(QString::fromUtf8(m_gameProcess->readAllStandardError()));
    });
    connect(m_gameProcess, &QProcess::started, this, [this]() {
        if (onGameStarted)
            onGameStarted();
    });
    connect(m_gameProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
                if (onGameFinished)
                    onGameFinished(code);
            });
}

void MinecraftInstaller::stop()
{
    m_cancelled = true;
}

void MinecraftInstaller::setProgress(const QString &stage, int done, int total,
                                     qint64 got, qint64 all, const QString &file)
{
    if (!onProgress)
        return;
    InstallProgress p;
    p.stage = stage;
    p.completedSteps = done;
    p.totalSteps = total;
    p.bytesGot = got;
    p.bytesTotal = all;
    p.currentFile = file;
    onProgress(p);
}

// ------------------------------------------------------------------ install

void MinecraftInstaller::install(const QString &versionId, const QString &loader,
                                 const QString &dataDir, const QString &gameDir,
                                 const std::function<QString(const QString &)> &urlResolver)
{
    if (m_busy)
        return;
    m_busy = true;
    m_cancelled = false;
    m_versionId = versionId;
    m_loader = loader;
    m_dataDir = dataDir;
    m_gameDir = gameDir;
    m_urlResolver = urlResolver;
    m_vi = QJsonObject();
    m_assetIndexId.clear();
    m_mainClass.clear();
    m_clientJar.clear();
    m_libraryJars.clear();
    m_nativeJars.clear();
    m_loaderJar.clear();
    m_requiredJavaMajor = 17;

    downloadVersionJson();
}

void MinecraftInstaller::downloadFile(const QUrl &url, const QString &destPath,
                                      std::function<void(bool, const QString &)> done)
{
    if (m_cancelled) {
        done(false, QStringLiteral("Отменено"));
        return;
    }
    // Уже скачано (и непустое) — пропускаем
    const qint64 existing = QFileInfo(destPath).size();
    if (existing > 0) {
        done(true, QString());
        return;
    }

    QDir().mkpath(QFileInfo(destPath).absolutePath());
    auto *save = new QSaveFile(destPath, this);
    if (!save->open(QIODevice::WriteOnly)) {
        delete save;
        done(false, QStringLiteral("Не удалось создать файл %1").arg(destPath));
        return;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(60000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("EnderForge/0.3 (Minecraft Launcher)"));
    QNetworkReply *reply = m_network->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, destPath](qint64 got, qint64 total) {
                setProgress(m_currentStage, m_currentDone, m_currentTotal,
                            got, total, QFileInfo(destPath).fileName());
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, save, done]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const QString err = reply->errorString();
            save->cancelWriting();
            save->deleteLater();
            done(false, err);
            return;
        }
        if (!save->commit()) {
            save->deleteLater();
            done(false, QStringLiteral("Не удалось записать файл"));
            return;
        }
        save->deleteLater();
        done(true, QString());
    });
}

void MinecraftInstaller::downloadVersionJson()
{
    const QString url = m_urlResolver ? m_urlResolver(m_versionId) : QString();
    if (url.isEmpty()) {
        finish(false, QStringLiteral("Версия %1 не найдена в списке").arg(m_versionId));
        return;
    }
    const QString dest = QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QStringLiteral(".json"));
    setProgress(QStringLiteral("описание версии"), 0, 1, 0, 0, m_versionId + QStringLiteral(".json"));
    downloadFile(QUrl(url), dest, [this, dest](bool ok, const QString &err) {
        if (!ok) {
            finish(false, QStringLiteral("Не удалось получить описание версии: %1").arg(err));
            return;
        }
        m_vi = QJsonDocument::fromJson(QFile(dest).readAll()).object();

        // Наследуемая версия (например, сборки Fabric): берём родительский JSON
        if (!m_vi.contains(QStringLiteral("downloads")) && m_vi.contains(QStringLiteral("inheritsFrom"))) {
            const QString parent = fromJson(m_vi, "inheritsFrom");
            const QString parentUrl = m_urlResolver ? m_urlResolver(parent) : QString();
            if (parentUrl.isEmpty()) {
                finish(false, QStringLiteral("Родительская версия %1 не найдена").arg(parent));
                return;
            }
            const QString parentDest =
                QDir(m_dataDir).filePath(QStringLiteral("versions/") + parent + QStringLiteral(".json"));
            downloadFile(QUrl(parentUrl), parentDest, [this, parent, parentDest](bool ok2, const QString &err2) {
                if (!ok2) {
                    finish(false, QStringLiteral("Родительская версия: %1").arg(err2));
                    return;
                }
                QJsonObject parentObj = QJsonDocument::fromJson(QFile(parentDest).readAll()).object();
                // libraries склеиваем (родитель + своя)
                QJsonArray libs;
                for (const QJsonValue &v : parentObj.value(QStringLiteral("libraries")).toArray())
                    libs.append(v);
                for (const QJsonValue &v : m_vi.value(QStringLiteral("libraries")).toArray())
                    libs.append(v);
                parentObj.insert(QStringLiteral("libraries"), libs);
                // поля ребёнка перекрывают родителя
                for (auto it = m_vi.constBegin(); it != m_vi.constEnd(); ++it)
                    parentObj.insert(it.key(), it.value());
                m_vi = parentObj;
                downloadClient();
            });
            return;
        }
        downloadClient();
    });
}

void MinecraftInstaller::downloadClient()
{
    const QJsonObject client =
        m_vi.value(QStringLiteral("downloads")).toObject().value(QStringLiteral("client")).toObject();
    const QString url = fromJson(client, "url");
    if (url.isEmpty()) {
        finish(false, QStringLiteral("В описании версии нет ссылки на клиент"));
        return;
    }
    m_clientJar = QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QLatin1Char('/') + m_versionId + QStringLiteral(".jar"));
    setProgress(QStringLiteral("клиент"), 1, 1, 0, 0, QFileInfo(m_clientJar).fileName());
    downloadFile(QUrl(url), m_clientJar, [this](bool ok, const QString &err) {
        if (!ok) {
            finish(false, QStringLiteral("Клиент: %1").arg(err));
            return;
        }
        downloadLibraries();
    });
}

struct LibraryTask {
    QUrl url;
    QString dest;
    bool isNative = false;
};

void MinecraftInstaller::downloadLibraries()
{
    const QJsonArray arr = m_vi.value(QStringLiteral("libraries")).toArray();
    QList<LibraryTask> tasks;

    for (const QJsonValue &v : arr) {
        const QJsonObject lib = v.toObject();
        if (!libraryAllowed(lib))
            continue;

        const QJsonObject downloads = lib.value(QStringLiteral("downloads")).toObject();

        // Natives: classifiers по платформе windows
        bool isNative = false;
        QString url, path;
        const QJsonObject natives = lib.value(QStringLiteral("natives")).toObject();
        if (natives.contains(QStringLiteral("windows"))) {
            QString classifier = fromJson(natives, "windows");
            const QJsonObject classifiers = downloads.value(QStringLiteral("classifiers")).toObject();
            QJsonObject art = classifiers.value(classifier).toObject();
            if (art.isEmpty() && !classifier.endsWith(QLatin1String("-64"))) {
                const QString alt = classifier + QStringLiteral("-64");
                art = classifiers.value(alt).toObject();
                if (!art.isEmpty())
                    classifier = alt;
            }
            if (!art.isEmpty()) {
                url = fromJson(art, "url");
                path = fromJson(art, "path");
                isNative = true;
            }
        } else {
            const QJsonObject artifact = downloads.value(QStringLiteral("artifact")).toObject();
            url = fromJson(artifact, "url");
            path = fromJson(artifact, "path");
        }

        // Старые версии: url задан базовым + maven-координаты
        if (url.isEmpty() || path.isEmpty()) {
            const QString base = lib.value(QStringLiteral("url")).toString();
            const QString name = fromJson(lib, "name");
            if (!base.isEmpty() && !name.isEmpty()) {
                QString classifier;
                const QJsonObject n2 = lib.value(QStringLiteral("natives")).toObject();
                if (n2.contains(QStringLiteral("windows")))
                    classifier = fromJson(n2, "windows");
                path = mavenPathFromName(name, classifier);
                if (!path.isEmpty()) {
                    url = base + QLatin1Char('/') + path;
                    isNative = !classifier.isEmpty();
                }
            }
        }

        if (url.isEmpty() || path.isEmpty())
            continue; // без скачиваемого артефакта

        const QString dest = QDir(m_dataDir).filePath(QStringLiteral("libraries/") + path);
        if (isNative)
            m_nativeJars.append(dest);
        else
            m_libraryJars.append(dest);
        tasks.append({ QUrl(url), dest, isNative });
    }

    if (tasks.isEmpty()) {
        downloadAssetIndex();
        return;
    }

    // Последовательное скачивание с прогрессом
    m_currentStage = QStringLiteral("библиотеки");
    m_currentTotal = tasks.size();
    std::function<void(int)> next = [this, tasks, &next](int i) {
        if (m_cancelled || i >= tasks.size()) {
            downloadAssetIndex();
            return;
        }
        m_currentDone = i;
        setProgress(m_currentStage, i, tasks.size(), 0, 0, QFileInfo(tasks.at(i).dest).fileName());
        downloadFile(tasks.at(i).url, tasks.at(i).dest, [this, tasks, i, &next](bool ok, const QString &err) {
            if (!ok) {
                finish(false, QStringLiteral("Библиотека: %1").arg(err));
                return;
            }
            next(i + 1);
        });
    };
    next(0);
}

void MinecraftInstaller::downloadAssetIndex()
{
    const QJsonObject idx = m_vi.value(QStringLiteral("assetIndex")).toObject();
    const QString url = fromJson(idx, "url");
    m_assetIndexId = fromJson(idx, "id");
    if (m_assetIndexId.isEmpty())
        m_assetIndexId = m_vi.value(QStringLiteral("assets")).toString();
    if (url.isEmpty() || m_assetIndexId.isEmpty()) {
        extractNatives(); // очень старые версии без assetIndex
        return;
    }
    const QString dest =
        QDir(m_dataDir).filePath(QStringLiteral("assets/indexes/") + m_assetIndexId + QStringLiteral(".json"));
    setProgress(QStringLiteral("индекс ресурсов"), 0, 1, 0, 0, m_assetIndexId + QStringLiteral(".json"));
    downloadFile(QUrl(url), dest, [this](bool ok, const QString &err) {
        if (!ok) {
            finish(false, QStringLiteral("Индекс ресурсов: %1").arg(err));
            return;
        }
        downloadAssets();
    });
}

void MinecraftInstaller::downloadAssets()
{
    const QString indexPath =
        QDir(m_dataDir).filePath(QStringLiteral("assets/indexes/") + m_assetIndexId + QStringLiteral(".json"));
    const QJsonObject index = QJsonDocument::fromJson(QFile(indexPath).readAll()).object();
    const QJsonObject objects = index.value(QStringLiteral("objects")).toObject();

    struct Asset { QString hash; qint64 size; };
    QList<Asset> assets;
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it)
        assets.append({ it.key(), static_cast<qint64>(it.value().toObject().value(QStringLiteral("size")).toDouble()) });

    if (assets.isEmpty()) {
        extractNatives();
        return;
    }

    m_currentStage = QStringLiteral("ресурсы");
    m_currentTotal = assets.size();
    m_assetsRoot = QDir(m_dataDir).filePath(QStringLiteral("assets"));

    std::function<void(int)> next = [this, assets, &next](int i) {
        if (m_cancelled || i >= assets.size()) {
            extractNatives();
            return;
        }
        const Asset &a = assets.at(i);
        const QString dest = m_assetsRoot + QStringLiteral("/objects/") + a.hash.left(2) + QLatin1Char('/') + a.hash;
        // Пропускаем уже скачанные (по размеру)
        if (QFileInfo(dest).size() == a.size) {
            m_currentDone = i;
            setProgress(m_currentStage, i, assets.size(), 0, 0, a.hash.left(8));
            next(i + 1);
            return;
        }
        m_currentDone = i;
        setProgress(m_currentStage, i, assets.size(), 0, 0, a.hash.left(8));
        const QUrl url(QString::fromLatin1(kAssetsUrl) + a.hash.left(2) + QLatin1Char('/') + a.hash);
        downloadFile(url, dest, [this, i, a, &next](bool ok, const QString &err) {
            if (!ok) {
                finish(false, QStringLiteral("Ресурс %1: %2").arg(a.hash.left(8), err));
                return;
            }
            next(i + 1);
        });
    };
    next(0);
}

void MinecraftInstaller::extractNatives()
{
    m_nativesDir = QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QStringLiteral("/natives"));
    QDir().mkpath(m_nativesDir);
    // Очистка
    for (const QFileInfo &f : QDir(m_nativesDir).entryInfoList(QDir::Files))
        QFile::remove(f.absoluteFilePath());

    // Windows 10+ содержит tar.exe (bsdtar), умеющий распаковывать zip/jar
    QString tar = QStringLiteral("tar");
    const QString sysTar = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(sysTar))
        tar = sysTar;

    std::function<void(int)> next = [this, tar, &next](int i) {
        if (m_cancelled || i >= m_nativeJars.size()) {
            resolveLoader();
            return;
        }
        setProgress(QStringLiteral("нативные библиотеки"), i, m_nativeJars.size(), 0, 0,
                    QFileInfo(m_nativeJars.at(i)).fileName());
        auto *p = new QProcess(this);
        p->setProgram(tar);
        p->setArguments({ QStringLiteral("-xf"), m_nativeJars.at(i), QStringLiteral("-C"), m_nativesDir });
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, p, i, &next](int code, QProcess::ExitStatus) {
                    p->deleteLater();
                    if (code != 0) {
                        finish(false, QStringLiteral("Не удалось распаковать natives: %1")
                                         .arg(QFileInfo(m_nativeJars.at(i)).fileName()));
                        return;
                    }
                    next(i + 1);
                });
        p->start();
    };
    if (m_nativeJars.isEmpty()) {
        resolveLoader();
        return;
    }
    next(0);
}

void MinecraftInstaller::resolveLoader()
{
    if (m_loader == QLatin1String("vanilla")) {
        m_mainClass = fromJson(m_vi, "mainClass");
        if (m_mainClass.isEmpty())
            m_mainClass = QStringLiteral("net.minecraft.client.main.Main");
        finish(true, QString());
        return;
    }
    if (m_loader == QLatin1String("forge") || m_loader == QLatin1String("neoforge")) {
        const QString name = (m_loader == QLatin1String("forge")) ? QStringLiteral("Forge")
                                                                  : QStringLiteral("NeoForge");
        finish(false, QStringLiteral("Запуск %1 пока не поддерживается — выберите Vanilla, Fabric или Quilt")
                          .arg(name));
        return;
    }

    // Fabric / Quilt: meta-сервис выдаёт версию загрузчика под версию MC
    const bool isFabric = (m_loader == QLatin1String("fabric"));
    const QString metaUrl = (isFabric ? QString::fromLatin1(kFabricMetaUrl)
                                      : QString::fromLatin1(kQuiltMetaUrl)) + m_versionId;
    const QString metaDest = QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QStringLiteral("/loader-meta.json"));
    setProgress(QStringLiteral("загрузчик"), 0, 1, 0, 0,
                (isFabric ? QStringLiteral("fabric") : QStringLiteral("quilt")));
    downloadFile(QUrl(metaUrl), metaDest, [this, isFabric, metaDest](bool ok, const QString &err) {
        if (!ok) {
            finish(false, QStringLiteral("Не удалось получить данные загрузчика: %1").arg(err));
            return;
        }
        const QJsonArray arr = QJsonDocument::fromJson(QFile(metaDest).readAll()).array();
        if (arr.isEmpty()) {
            finish(false, QStringLiteral("Загрузчик недоступен для версии %1").arg(m_versionId));
            return;
        }
        const QJsonObject first = arr.first().toObject();
        const QString loaderVersion = fromJson(first.value(QStringLiteral("loader")).toObject(), "version");
        m_mainClass = fromJson(first.value(QStringLiteral("launcherMeta")).toObject(), "mainClass");
        if (m_mainClass.isEmpty())
            m_mainClass = isFabric
                ? QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient")
                : QStringLiteral("org.quiltmc.loader.impl.launch.knot.KnotClient");
        if (loaderVersion.isEmpty()) {
            finish(false, QStringLiteral("Нет версии загрузчика"));
            return;
        }
        const QString jarPath = isFabric
            ? QStringLiteral("net/fabricmc/fabric-loader/%1/fabric-loader-%1.jar").arg(loaderVersion)
            : QStringLiteral("org/quiltmc/quilt-loader/%1/quilt-loader-%1.jar").arg(loaderVersion);
        const QString jarUrl = (isFabric ? QString::fromLatin1(kFabricMavenUrl)
                                         : QString::fromLatin1(kQuiltMavenUrl)) + jarPath;
        m_loaderJar = QDir(m_dataDir).filePath(QStringLiteral("libraries/") + jarPath);
        setProgress(QStringLiteral("загрузчик"), 0, 1, 0, 0, QFileInfo(m_loaderJar).fileName());
        downloadFile(QUrl(jarUrl), m_loaderJar, [this](bool ok2, const QString &err2) {
            if (!ok2) {
                finish(false, QStringLiteral("Загрузчик: %1").arg(err2));
                return;
            }
            finish(true, QString());
        });
    });
}

void MinecraftInstaller::finish(bool ok, const QString &err)
{
    m_busy = false;
    if (ok) {
        // Сохраняем информацию для запуска
        QJsonObject info;
        info.insert(QStringLiteral("mainClass"), m_mainClass);
        info.insert(QStringLiteral("loaderJar"), m_loaderJar);
        info.insert(QStringLiteral("assetIndex"), m_assetIndexId);
        info.insert(QStringLiteral("clientJar"), m_clientJar);
        const QString infoPath =
            QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QStringLiteral("/launch.json"));
        QDir().mkpath(QFileInfo(infoPath).absolutePath());
        QFile f(infoPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QJsonDocument(info).toJson(QJsonDocument::Indented));
    }
    if (onFinished)
        onFinished(ok, err);
}

// --------------------------------------------------------------------- Java

QString MinecraftInstaller::javaMajorVersion(const QString &javaExe)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(javaExe, { QStringLiteral("-version") });
    if (!p.waitForFinished(10000))
        return QString();
    const QString out = QString::fromUtf8(p.readAll());
    QRegularExpression re(QStringLiteral("\"(\\d+)(?:\\.(\\d+))?"));
    const auto m = re.match(out);
    if (!m.hasMatch())
        return QString();
    int major = m.captured(1).toInt();
    if (major == 1) // "1.8.0_…" => 8
        major = m.captured(2).toInt();
    return QString::number(major);
}

QString MinecraftInstaller::findJava()
{
    // Кэш предыдущего поиска (если мы сами скачали JRE)
    const QString cachedPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                   .filePath(QStringLiteral("runtime/java-path.txt"));
    QFile cf(cachedPath);
    if (cf.open(QIODevice::ReadOnly)) {
        const QString p = QString::fromUtf8(cf.readAll()).trimmed();
        if (!p.isEmpty() && QFileInfo::exists(p))
            return p;
    }

    QStringList roots;
    const QString pf = qEnvironmentVariable("ProgramFiles");
    const QString pfx = qEnvironmentVariable("ProgramFiles(x86)");
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (!pf.isEmpty()) roots << pf;
    if (!pfx.isEmpty()) roots << pfx;
    if (!local.isEmpty()) roots << QDir(local).filePath(QStringLiteral("Programs"));

    QStringList candidates;

    // Стандартные каталоги установки Java
    const QStringList patterns = {
        QStringLiteral("Java/jdk-*"), QStringLiteral("Java/jre-*"),
        QStringLiteral("Eclipse Adoptium/*"), QStringLiteral("Microsoft/jdk-*"),
        QStringLiteral("Zulu/*"), QStringLiteral("jdk-*"), QStringLiteral("microsoft-jdk-*"),
        QStringLiteral("Java/*"),
    };
    for (const QString &root : roots) {
        for (const QString &pattern : patterns) {
            const QDir d(QDir(root).filePath(pattern));
            for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString java = QDir(QDir(root).filePath(pattern + QLatin1Char('/') + sub))
                                         .filePath(QStringLiteral("bin/java.exe"));
                if (QFileInfo::exists(java))
                    candidates << java;
            }
        }
    }

    // JAVA_HOME
    const QString jh = qEnvironmentVariable("JAVA_HOME");
    if (!jh.isEmpty()) {
        const QString java = QDir(jh).filePath(QStringLiteral("bin/java.exe"));
        if (QFileInfo::exists(java))
            candidates << java;
    }

    // `where java`
    QProcess where;
    where.setProcessChannelMode(QProcess::SeparateChannels);
    where.start(QStringLiteral("where"), { QStringLiteral("java") });
    if (where.waitForFinished(8000)) {
        const QStringList lines = QString::fromUtf8(where.readAllStandardOutput()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString t = line.trimmed();
            if (t.endsWith(QLatin1String("java.exe"), Qt::CaseInsensitive))
                candidates << t;
        }
    }

    // Выбираем Java с максимальной версией, предпочитая >= 17
    QString best;
    int bestMajor = -1;
    for (const QString &c : candidates) {
        const int major = javaMajorVersion(c).toInt();
        if (major <= 0)
            continue;
        if (major > bestMajor || (major >= 17 && bestMajor < 17)) {
            best = c;
            bestMajor = major;
        }
    }
    return best;
}

void MinecraftInstaller::downloadJre(const QString &dataDir,
                                     const std::function<void(bool, const QString &)> &done)
{
    const QString runtimeDir = QDir(dataDir).filePath(QStringLiteral("runtime"));
    QDir().mkpath(runtimeDir);
    const QString zipPath = QDir(runtimeDir).filePath(QStringLiteral("jre17.zip"));
    setProgress(QStringLiteral("java"), 0, 1, 0, 0, QStringLiteral("jre17.zip"));
    downloadFile(QUrl(QString::fromLatin1(kJreUrl)), zipPath, [this, runtimeDir, zipPath, done](bool ok, const QString &err) {
        if (!ok) {
            done(false, QStringLiteral("Не удалось скачать Java: %1").arg(err));
            return;
        }
        extractZipWithTar(zipPath, runtimeDir, [this, runtimeDir, zipPath, done](bool ok2) {
            if (!ok2) {
                done(false, QStringLiteral("Не удалось распаковать Java"));
                return;
            }
            // Найти bin/java.exe в распакованной папке
            QString javaPath;
            for (const QString &sub : QDir(runtimeDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                const QString cand = QDir(runtimeDir).filePath(sub + QStringLiteral("/bin/java.exe"));
                if (QFileInfo::exists(cand)) {
                    javaPath = cand;
                    break;
                }
            }
            if (javaPath.isEmpty()) {
                done(false, QStringLiteral("В скачанной Java не найден java.exe"));
                return;
            }
            m_javaPath = javaPath;
            QFile pf(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                         .filePath(QStringLiteral("runtime/java-path.txt")));
            if (pf.open(QIODevice::WriteOnly | QIODevice::Truncate))
                pf.write(javaPath.toUtf8());
            done(true, QString());
        });
    });
}

void MinecraftInstaller::extractZipWithTar(const QString &zipPath, const QString &destDir,
                                           std::function<void(bool)> done)
{
    QString tar = QStringLiteral("tar");
    const QString sysTar = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(sysTar))
        tar = sysTar;
    auto *p = new QProcess(this);
    p->setProgram(tar);
    p->setArguments({ QStringLiteral("-xf"), zipPath, QStringLiteral("-C"), destDir });
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p, done](int code, QProcess::ExitStatus) {
                p->deleteLater();
                done(code == 0);
            });
    p->start();
}

void MinecraftInstaller::ensureJava(const QString &dataDir,
                                    const std::function<void(bool, const QString &)> &done)
{
    const QString found = findJava();
    if (!found.isEmpty()) {
        m_javaPath = found;
        done(true, QString());
        return;
    }
    downloadJre(dataDir, done);
}

// ------------------------------------------------------------------ launch

bool MinecraftInstaller::loadLaunchInfo(const QString &versionId, const QString &dataDir)
{
    const QString infoPath =
        QDir(dataDir).filePath(QStringLiteral("versions/") + versionId + QStringLiteral("/launch.json"));
    QFile f(infoPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject info = QJsonDocument::fromJson(f.readAll()).object();
    m_mainClass = fromJson(info, "mainClass");
    m_loaderJar = fromJson(info, "loaderJar");
    m_assetIndexId = fromJson(info, "assetIndex");
    m_clientJar = fromJson(info, "clientJar");
    if (m_clientJar.isEmpty())
        m_clientJar = QDir(dataDir).filePath(QStringLiteral("versions/") + versionId + QLatin1Char('/') + versionId + QStringLiteral(".jar"));
    m_assetsRoot = QDir(dataDir).filePath(QStringLiteral("assets"));
    m_nativesDir = QDir(dataDir).filePath(QStringLiteral("versions/") + versionId + QStringLiteral("/natives"));
    return !m_mainClass.isEmpty() && QFileInfo::exists(m_clientJar);
}

bool MinecraftInstaller::buildClasspath(const QString &versionId, const QString &dataDir)
{
    m_classpath.clear();
    m_classpath << m_clientJar;

    // Библиотеки из version JSON (только artifact, без natives)
    const QString vjPath = QDir(dataDir).filePath(QStringLiteral("versions/") + versionId + QStringLiteral(".json"));
    const QJsonObject vi = QJsonDocument::fromJson(QFile(vjPath).readAll()).object();
    const QJsonArray arr = vi.value(QStringLiteral("libraries")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject lib = v.toObject();
        if (!libraryAllowed(lib))
            continue;
        const QJsonObject downloads = lib.value(QStringLiteral("downloads")).toObject();
        if (lib.value(QStringLiteral("natives")).isObject())
            continue; // natives в classpath не идут
        QString path = fromJson(downloads.value(QStringLiteral("artifact")).toObject(), "path");
        if (path.isEmpty()) {
            const QString base = lib.value(QStringLiteral("url")).toString();
            const QString name = fromJson(lib, "name");
            if (!base.isEmpty() && !name.isEmpty())
                path = mavenPathFromName(name, QString());
        }
        if (!path.isEmpty()) {
            const QString abs = QDir(dataDir).filePath(QStringLiteral("libraries/") + path);
            if (QFileInfo::exists(abs))
                m_classpath << abs;
        }
    }
    if (!m_loaderJar.isEmpty() && QFileInfo::exists(m_loaderJar))
        m_classpath << m_loaderJar;
    return m_classpath.size() > 1;
}

bool MinecraftInstaller::startProcess()
{
    if (!QFileInfo::exists(m_javaPath))
        m_javaPath = findJava();
    if (m_javaPath.isEmpty())
        return false;

    QStringList args;
    args << QStringLiteral("-Xmx2G") << QStringLiteral("-Xms512M");
    args << QStringLiteral("-Djava.library.path=%1").arg(m_nativesDir);
    args << QStringLiteral("-cp") << m_classpath.join(QLatin1Char(';'));
    args << m_mainClass;
    args << QStringLiteral("--username") << QStringLiteral("Player")
         << QStringLiteral("--version") << m_versionId
         << QStringLiteral("--gameDir") << m_gameDir
         << QStringLiteral("--assetsDir") << m_assetsRoot;
    if (!m_assetIndexId.isEmpty())
        args << QStringLiteral("--assetIndex") << m_assetIndexId;
    args << QStringLiteral("--uuid") << QStringLiteral("00000000-0000-0000-0000-000000000000")
         << QStringLiteral("--accessToken") << QStringLiteral("0")
         << QStringLiteral("--userType") << QStringLiteral("legacy");

    m_gameProcess->setProgram(m_javaPath);
    m_gameProcess->setArguments(args);
    m_gameProcess->setWorkingDirectory(m_gameDir);
    QDir().mkpath(m_gameDir);
    m_gameProcess->start();
    return true;
}

bool MinecraftInstaller::launchGame(const QString &versionId, const QString &dataDir,
                                    const QString &gameDir)
{
    if (m_gameProcess->state() != QProcess::NotRunning)
        return false; // уже запущена

    if (!loadLaunchInfo(versionId, dataDir))
        return false;
    if (!buildClasspath(versionId, dataDir))
        return false;

    m_versionId = versionId;
    m_gameDir = gameDir;

    if (m_javaPath.isEmpty())
        m_javaPath = findJava();
    if (m_javaPath.isEmpty())
        return false;

    return startProcess();
}

void MinecraftInstaller::stopGame()
{
    if (m_gameProcess && m_gameProcess->state() != QProcess::NotRunning) {
        m_gameProcess->kill();
        m_gameProcess->waitForFinished(3000);
    }
}

bool MinecraftInstaller::isGameRunning() const
{
    return m_gameProcess && m_gameProcess->state() != QProcess::NotRunning;
}
