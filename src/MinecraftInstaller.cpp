#include "MinecraftInstaller.h"
#include "NetUtil.h"

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

QJsonDocument readJsonFile(const QString &path, QString *errOut);

constexpr const char *kAssetsUrl = "https://resources.download.minecraft.net/";
constexpr const char *kFabricMetaUrl = "https://meta.fabricmc.net/v2/versions/loader/";
constexpr const char *kFabricMavenUrl = "https://maven.fabricmc.net/";
constexpr const char *kQuiltMetaUrl = "https://meta.quiltmc.org/v3/versions/loader/";
constexpr const char *kQuiltMavenUrl = "https://maven.quiltmc.org/repository/release/";

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

// Мажорная версия Java по ID версии Minecraft. Используется как запасной
// вариант, если в version JSON нет поля javaVersion.majorVersion.
int inferJavaMajor(const QString &versionId)
{
    QRegularExpression re(QStringLiteral("^(\\d+)(?:\\.(\\d+))?(?:\\.(\\d+))?"));
    const auto m = re.match(versionId);
    if (!m.hasMatch())
        return 17;

    const int first = m.captured(1).toInt();
    if (first == 1) {
        const int minor = m.captured(2).toInt();
        const int patch = m.captured(3).isEmpty() ? -1 : m.captured(3).toInt();
        if (minor >= 21)
            return 21;
        if (minor == 20)
            return patch >= 5 ? 21 : 17; // 1.20.5+ → Java 21
        if (minor >= 17)
            return 17;
        return 8; // старые версии (1.16 и ниже) — Java 8
    }
    // Новая нумерация Mojang: 26.x/25.x → Java 25, 23.x/24.x → 23, 21.x/22.x → 21
    if (first >= 25)
        return 25;
    if (first >= 23)
        return 23;
    if (first >= 21)
        return 21;
    return 17;
}

int requiredJavaMajorFromJson(const QJsonObject &vi, const QString &versionId)
{
    const QJsonObject jv = vi.value(QStringLiteral("javaVersion")).toObject();
    const int major = jv.value(QStringLiteral("majorVersion")).toInt(0);
    return major > 0 ? major : inferJavaMajor(versionId);
}

int requiredJavaMajorFromFiles(const QString &dataDir, const QString &versionId)
{
    // Сначала launch.json (сохраняется при установке), затем исходный version JSON.
    const QString launchPath = QDir(dataDir).filePath(
        QStringLiteral("versions/") + versionId + QStringLiteral("/launch.json"));
    QJsonObject info = readJsonFile(launchPath, nullptr).object();
    int major = info.value(QStringLiteral("requiredJavaMajor")).toInt(0);
    if (major > 0)
        return major;

    const QString vjPath = QDir(dataDir).filePath(
        QStringLiteral("versions/") + versionId + QStringLiteral(".json"));
    const QJsonObject vi = readJsonFile(vjPath, nullptr).object();
    return requiredJavaMajorFromJson(vi, versionId);
}

QString jreUrlForMajor(int major)
{
    return QStringLiteral(
               "https://api.adoptium.net/v3/binary/latest/%1/ga/windows/x64/jre/hotspot/normal/eclipse")
        .arg(major);
}

QJsonObject mavenLibrary(const QString &maven, const QString &defaultBaseUrl)
{
    QJsonObject lib;
    lib.insert(QStringLiteral("name"), maven);
    lib.insert(QStringLiteral("url"), defaultBaseUrl);
    return lib;
}

QByteArray readAllFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

QByteArray peekFile(const QString &path, qint64 maxBytes = 512)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.read(maxBytes);
}

QJsonDocument readJsonFile(const QString &path, QString *errOut)
{
    const QByteArray data = readAllFile(path);
    if (isBadDownloadPayload(data)) {
        if (errOut)
            *errOut = describeBadPayload(data);
        return {};
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError) {
        if (errOut)
            *errOut = describeBadPayload(data);
        return {};
    }
    return doc;
}

void configureDownloadRequest(QNetworkRequest *request, int timeoutMs)
{
    request->setTransferTimeout(timeoutMs);
    request->setMaximumRedirectsAllowed(10);
    request->setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);
    request->setHeader(QNetworkRequest::UserAgentHeader,
                       QStringLiteral("EnderForge/0.5 (Minecraft Launcher)"));
    request->setRawHeader("Accept", "*/*");
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
    if (m_activeReply)
        m_activeReply->abort();
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

void MinecraftInstaller::determineJavaMajor()
{
    m_requiredJavaMajor = requiredJavaMajorFromJson(m_vi, m_versionId);
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
    m_fileTasks.clear();
    m_taskIndex = 0;
    m_nativeIndex = 0;

    downloadVersionJson();
}

void MinecraftInstaller::downloadFile(const QUrl &url, const QString &destPath,
                                      std::function<void(bool, const QString &)> done,
                                      qint64 expectedSize)
{
    if (m_cancelled) {
        done(false, QStringLiteral("Отменено"));
        return;
    }

    const qint64 existing = QFileInfo(destPath).size();
    if (existing > 0) {
        const QByteArray head = peekFile(destPath, 512);
        const bool sizeOk = expectedSize <= 0 || existing == expectedSize;
        if (sizeOk && !payloadLooksLikeHtml(head)) {
            done(true, QString());
            return;
        }
        QFile::remove(destPath);
    }

    QDir().mkpath(QFileInfo(destPath).absolutePath());
    auto *save = new QSaveFile(destPath, this);
    if (!save->open(QIODevice::WriteOnly)) {
        delete save;
        done(false, QStringLiteral("Не удалось создать файл %1").arg(destPath));
        return;
    }

    QNetworkRequest request(url);
    configureDownloadRequest(&request, 300000);
    QNetworkReply *reply = m_network->get(request);
    m_activeReply = reply;

    // Пишем тело ответа: потоково (readyRead) + остаток в finished.
    // Иначе QSaveFile коммитит 0 байт.
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

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, destPath](qint64 got, qint64 total) {
                setProgress(m_currentStage, m_currentDone, m_currentTotal,
                            got, total, QFileInfo(destPath).fileName());
            });
    connect(reply, &QNetworkReply::readyRead, this, [reply, writeChunk]() {
        writeChunk(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, save, done, expectedSize, writeChunk, head, written]() {
        if (m_activeReply == reply)
            m_activeReply = nullptr;
        writeChunk(reply->readAll());
        reply->deleteLater();

        const QByteArray preview = *head;
        const qint64 n = *written;
        delete head;
        delete written;

        if (reply->error() != QNetworkReply::NoError) {
            save->cancelWriting();
            save->deleteLater();
            done(false, reply->errorString());
            return;
        }
        if (n <= 0 || payloadLooksLikeHtml(preview)) {
            save->cancelWriting();
            save->deleteLater();
            done(false, describeBadPayload(preview));
            return;
        }
        if (expectedSize > 0 && n < expectedSize) {
            save->cancelWriting();
            save->deleteLater();
            done(false, QStringLiteral("Файл обрезан: %1 из %2 байт").arg(n).arg(expectedSize));
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
        QString parseErr;
        m_vi = readJsonFile(dest, &parseErr).object();
        if (m_vi.isEmpty()) {
            finish(false, QStringLiteral("Не удалось получить описание версии: %1")
                              .arg(parseErr.isEmpty() ? describeBadPayload(readAllFile(dest)) : parseErr));
            return;
        }

        // Наследуемая версия (например, сборки Fabric): берём родительский JSON
        determineJavaMajor();
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
                QString parentErr;
                QJsonObject parentObj = readJsonFile(parentDest, &parentErr).object();
                if (parentObj.isEmpty()) {
                    finish(false, QStringLiteral("Родительская версия: %1")
                                      .arg(parentErr.isEmpty() ? describeBadPayload(readAllFile(parentDest))
                                                               : parentErr));
                    return;
                }
                // libraries склеиваем (родитель + своя)
                QJsonArray libs;
                for (const QJsonValue &v : parentObj.value(QStringLiteral("libraries")).toArray())
                    libs.append(v);
                for (const QJsonValue &v : m_vi.value(QStringLiteral("libraries")).toArray())
                    libs.append(v);
                parentObj.insert(QStringLiteral("libraries"), libs);
                // Поля ребёнка перекрывают родителя, но НЕ libraries — иначе
                // родительские библиотеки теряются и запуск падает.
                for (auto it = m_vi.constBegin(); it != m_vi.constEnd(); ++it) {
                    if (it.key() == QLatin1String("libraries"))
                        continue;
                    parentObj.insert(it.key(), it.value());
                }
                m_vi = parentObj;
                determineJavaMajor();
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
    const qint64 clientSize = static_cast<qint64>(client.value(QStringLiteral("size")).toDouble(0));
    m_clientJar = QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QLatin1Char('/') + m_versionId + QStringLiteral(".jar"));
    setProgress(QStringLiteral("клиент"), 1, 1, 0, 0, QFileInfo(m_clientJar).fileName());
    downloadFile(QUrl(url), m_clientJar, [this](bool ok, const QString &err) {
        if (!ok) {
            finish(false, QStringLiteral("Клиент: %1").arg(err));
            return;
        }
        downloadLibraries();
    }, clientSize);
}

void MinecraftInstaller::downloadLibraries()
{
    const QJsonArray arr = m_vi.value(QStringLiteral("libraries")).toArray();
    m_fileTasks.clear();
    m_taskIndex = 0;

    for (const QJsonValue &v : arr) {
        const QJsonObject lib = v.toObject();
        if (!libraryAllowed(lib))
            continue;

        const QJsonObject downloads = lib.value(QStringLiteral("downloads")).toObject();

        // Natives: classifiers по платформе windows
        bool isNative = false;
        QString url, path;
        qint64 size = 0;
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
                size = static_cast<qint64>(art.value(QStringLiteral("size")).toDouble(0));
                isNative = true;
            }
        } else {
            const QJsonObject artifact = downloads.value(QStringLiteral("artifact")).toObject();
            url = fromJson(artifact, "url");
            path = fromJson(artifact, "path");
            size = static_cast<qint64>(artifact.value(QStringLiteral("size")).toDouble(0));
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
        m_fileTasks.append({ url, dest, size });
    }

    if (m_fileTasks.isEmpty()) {
        downloadAssetIndex();
        return;
    }

    m_currentStage = QStringLiteral("библиотеки");
    m_currentTotal = m_fileTasks.size();
    downloadNextLibrary();
}

void MinecraftInstaller::downloadNextLibrary()
{
    if (m_cancelled || m_taskIndex >= m_fileTasks.size()) {
        downloadAssetIndex();
        return;
    }
    const FileTask t = m_fileTasks.at(m_taskIndex);
    m_currentDone = m_taskIndex;
    setProgress(m_currentStage, m_taskIndex, m_fileTasks.size(), 0, 0,
                QFileInfo(t.dest).fileName());
    downloadFile(QUrl(t.url), t.dest, [this](bool ok, const QString &err) {
        if (!ok) {
            finish(false, QStringLiteral("Библиотека: %1").arg(err));
            return;
        }
        ++m_taskIndex;
        downloadNextLibrary();
    }, t.expectedSize);
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
    QString parseErr;
    const QJsonObject index = readJsonFile(indexPath, &parseErr).object();
    if (index.isEmpty()) {
        finish(false, QStringLiteral("Индекс ресурсов: %1")
                          .arg(parseErr.isEmpty() ? describeBadPayload(readAllFile(indexPath)) : parseErr));
        return;
    }
    const QJsonObject objects = index.value(QStringLiteral("objects")).toObject();

    m_fileTasks.clear();
    m_taskIndex = 0;
    m_assetsRoot = QDir(m_dataDir).filePath(QStringLiteral("assets"));

    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        // В манифесте ключ — имя ресурса, hash лежит в поле "hash"
        const QString hash = obj.value(QStringLiteral("hash")).toString();
        const qint64 size = static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble(0));
        if (hash.size() < 2)
            continue;
        const QString dest = m_assetsRoot + QStringLiteral("/objects/") + hash.left(2) + QLatin1Char('/') + hash;
        const QString url = QString::fromLatin1(kAssetsUrl) + hash.left(2) + QLatin1Char('/') + hash;
        m_fileTasks.append({ url, dest, size });
    }

    if (m_fileTasks.isEmpty()) {
        extractNatives();
        return;
    }

    m_currentStage = QStringLiteral("ресурсы");
    m_currentTotal = m_fileTasks.size();
    downloadNextAsset();
}

void MinecraftInstaller::downloadNextAsset()
{
    while (!m_cancelled && m_taskIndex < m_fileTasks.size()) {
        const FileTask t = m_fileTasks.at(m_taskIndex);
        m_currentDone = m_taskIndex;
        const QString shortHash = QFileInfo(t.dest).fileName().left(8);
        setProgress(m_currentStage, m_taskIndex, m_fileTasks.size(), 0, 0, shortHash);
        if (t.expectedSize > 0 && QFileInfo(t.dest).size() == t.expectedSize) {
            ++m_taskIndex;
            continue;
        }
        downloadFile(QUrl(t.url), t.dest, [this, shortHash](bool ok, const QString &err) {
            if (!ok) {
                finish(false, QStringLiteral("Ресурс %1: %2").arg(shortHash, err));
                return;
            }
            ++m_taskIndex;
            downloadNextAsset();
        }, t.expectedSize);
        return;
    }
    extractNatives();
}

void MinecraftInstaller::extractNatives()
{
    m_nativesDir = QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QStringLiteral("/natives"));
    QDir().mkpath(m_nativesDir);
    for (const QFileInfo &f : QDir(m_nativesDir).entryInfoList(QDir::Files))
        QFile::remove(f.absoluteFilePath());

    m_nativeIndex = 0;
    extractNextNative();
}

void MinecraftInstaller::extractNextNative()
{
    if (m_cancelled || m_nativeIndex >= m_nativeJars.size()) {
        resolveLoader();
        return;
    }

    QString tar = QStringLiteral("tar");
    const QString sysTar = QStringLiteral("C:/Windows/System32/tar.exe");
    if (QFileInfo::exists(sysTar))
        tar = sysTar;

    setProgress(QStringLiteral("нативные библиотеки"), m_nativeIndex, m_nativeJars.size(), 0, 0,
                QFileInfo(m_nativeJars.at(m_nativeIndex)).fileName());
    auto *p = new QProcess(this);
    p->setProgram(tar);
    p->setArguments({ QStringLiteral("-xf"), m_nativeJars.at(m_nativeIndex),
                      QStringLiteral("-C"), m_nativesDir });
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p](int code, QProcess::ExitStatus) {
                p->deleteLater();
                if (code != 0) {
                    finish(false, QStringLiteral("Не удалось распаковать natives: %1")
                                     .arg(QFileInfo(m_nativeJars.at(m_nativeIndex)).fileName()));
                    return;
                }
                ++m_nativeIndex;
                extractNextNative();
            });
    p->start();
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

    // Fabric / Quilt: meta-сервис выдаёт версию загрузчика и mappings под версию MC.
    // Сначала получаем рекомендуемую версию загрузчика из списка, затем полные
    // данные (launcherMeta: mainClass + библиотеки загрузчика) для этой версии.
    const bool isFabric = (m_loader == QLatin1String("fabric"));
    const QString mavenBase = (isFabric ? QString::fromLatin1(kFabricMavenUrl)
                                        : QString::fromLatin1(kQuiltMavenUrl));
    const QString metaUrl = (isFabric ? QString::fromLatin1(kFabricMetaUrl)
                                      : QString::fromLatin1(kQuiltMetaUrl)) + m_versionId;
    const QString metaDest =
        QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QStringLiteral("/loader-meta.json"));
    setProgress(QStringLiteral("загрузчик"), 0, 1, 0, 0,
                (isFabric ? QStringLiteral("fabric") : QStringLiteral("quilt")));
    downloadFile(QUrl(metaUrl), metaDest, [this, isFabric, mavenBase, metaUrl, metaDest](bool ok, const QString &err) {
        if (!ok) {
            finish(false, QStringLiteral("Не удалось получить данные загрузчика: %1").arg(err));
            return;
        }

        QString parseErr;
        const QJsonArray arr = readJsonFile(metaDest, &parseErr).array();
        if (arr.isEmpty()) {
            finish(false, parseErr.isEmpty()
                              ? QStringLiteral("Загрузчик недоступен для версии %1").arg(m_versionId)
                              : QStringLiteral("Не удалось получить данные загрузчика: %1").arg(parseErr));
            return;
        }

        const QJsonObject first = arr.first().toObject();
        const QJsonObject loaderObj = first.value(QStringLiteral("loader")).toObject();
        const QString loaderVersion = fromJson(loaderObj, "version");
        if (loaderVersion.isEmpty()) {
            finish(false, QStringLiteral("Нет версии загрузчика"));
            return;
        }

        // Maven-координаты загрузчика и mapping-слоя (intermediary/hashed).
        QString loaderMaven = fromJson(loaderObj, "maven");
        if (loaderMaven.isEmpty())
            loaderMaven = QString(isFabric ? QStringLiteral("net.fabricmc:fabric-loader:")
                                           : QStringLiteral("org.quiltmc:quilt-loader:")) + loaderVersion;

        QString mappingMaven;
        const QString mappingField = isFabric ? QStringLiteral("intermediary") : QStringLiteral("hashed");
        const QJsonObject mappingObj = first.value(mappingField).toObject();
        mappingMaven = fromJson(mappingObj, "maven");
        if (mappingMaven.isEmpty() && !fromJson(mappingObj, "version").isEmpty()) {
            mappingMaven = QString(isFabric ? QStringLiteral("net.fabricmc:intermediary:")
                                            : QStringLiteral("org.quiltmc:hashed:"))
                               + fromJson(mappingObj, "version");
        }

        // Полные данные загрузчика (mainClass + зависимые библиотеки).
        const QString detailUrl = metaUrl + QLatin1Char('/') + loaderVersion;
        const QString detailDest =
            QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QStringLiteral("/loader-details.json"));
        downloadFile(QUrl(detailUrl), detailDest, [this, isFabric, mavenBase, mappingMaven, loaderMaven,
                                                   detailDest](bool ok2, const QString &err2) {
            // Фолбэк: даже если подробные данные недоступны, ставим
            // загрузчик + mapping и пробуем запуск (mainClass берём по умолчанию).
            QJsonObject detail;
            if (ok2) {
                QJsonDocument doc = readJsonFile(detailDest, nullptr);
                if (!doc.isNull() && doc.isObject())
                    detail = doc.object();
                else if (!doc.isNull() && doc.isArray())
                    detail = doc.array().first().toObject();
            }

            const QJsonObject launcherMeta = detail.value(QStringLiteral("launcherMeta")).toObject();
            m_mainClass = fromJson(launcherMeta.value(QStringLiteral("mainClass")).toObject(), "client");
            if (m_mainClass.isEmpty())
                m_mainClass = isFabric
                    ? QStringLiteral("net.fabricmc.loader.impl.launch.knot.KnotClient")
                    : QStringLiteral("org.quiltmc.loader.launch.knot.KnotClient");

            // Собираем все библиотеки загрузчика в version JSON, чтобы при
            // запуске они попали в classpath.
            QJsonArray libs = m_vi.value(QStringLiteral("libraries")).toArray();
            m_fileTasks.clear();
            m_taskIndex = 0;

            const auto addMavenLibrary = [&](const QString &maven, const QString &baseUrl) {
                const QString path = mavenPathFromName(maven, QString());
                if (path.isEmpty())
                    return;
                libs.append(mavenLibrary(maven, baseUrl));
                const QString dest = QDir(m_dataDir).filePath(QStringLiteral("libraries/") + path);
                m_libraryJars.append(dest);
                m_fileTasks.append({ baseUrl + QLatin1Char('/') + path, dest, 0 });
            };

            if (!mappingMaven.isEmpty())
                addMavenLibrary(mappingMaven, mavenBase);

            const QJsonObject loaderLibraries =
                launcherMeta.value(QStringLiteral("libraries")).toObject();
            const auto addMetaLibraries = [&](const QString &key) {
                for (const QJsonValue &v : loaderLibraries.value(key).toArray()) {
                    const QJsonObject lib = v.toObject();
                    const QString name = fromJson(lib, "name");
                    if (name.isEmpty())
                        continue;
                    const QString base = fromJson(lib, "url").isEmpty() ? mavenBase : fromJson(lib, "url");
                    const QString path = mavenPathFromName(name, QString());
                    if (path.isEmpty())
                        continue;
                    libs.append(lib);
                    const QString dest = QDir(m_dataDir).filePath(QStringLiteral("libraries/") + path);
                    m_libraryJars.append(dest);
                    m_fileTasks.append({ base + QLatin1Char('/') + path, dest, 0 });
                }
            };
            addMetaLibraries(QStringLiteral("common"));
            addMetaLibraries(QStringLiteral("client"));

            m_vi.insert(QStringLiteral("libraries"), libs);

            const QString loaderPath = mavenPathFromName(loaderMaven, QString());
            if (loaderPath.isEmpty()) {
                finish(false, QStringLiteral("Не удалось определить jar загрузчика"));
                return;
            }
            m_loaderJar = QDir(m_dataDir).filePath(QStringLiteral("libraries/") + loaderPath);
            const QString loaderUrl = mavenBase + QLatin1Char('/') + loaderPath;
            setProgress(QStringLiteral("загрузчик"), 0, 1, 0, 0, QFileInfo(m_loaderJar).fileName());
            downloadFile(QUrl(loaderUrl), m_loaderJar, [this](bool ok3, const QString &err3) {
                if (!ok3) {
                    finish(false, QStringLiteral("Загрузчик: %1").arg(err3));
                    return;
                }
                downloadLoaderLibraries();
            });
        });
    });
}

void MinecraftInstaller::downloadLoaderLibraries()
{
    if (m_cancelled)
        return;
    m_currentStage = QStringLiteral("загрузчик");
    m_currentTotal = m_fileTasks.size();
    downloadNextLoaderLibrary();
}

void MinecraftInstaller::downloadNextLoaderLibrary()
{
    if (m_cancelled || m_taskIndex >= m_fileTasks.size()) {
        finish(true, QString());
        return;
    }
    const FileTask t = m_fileTasks.at(m_taskIndex);
    m_currentDone = m_taskIndex;
    setProgress(m_currentStage, m_taskIndex, m_fileTasks.size(), 0, 0,
                QFileInfo(t.dest).fileName());
    downloadFile(QUrl(t.url), t.dest, [this](bool ok, const QString &err) {
        if (!ok) {
            finish(false, QStringLiteral("Библиотека загрузчика: %1").arg(err));
            return;
        }
        ++m_taskIndex;
        downloadNextLoaderLibrary();
    }, t.expectedSize);
}

void MinecraftInstaller::finish(bool ok, const QString &err)
{
    m_busy = false;
    if (ok) {
        // Сохраняем объединённый version JSON (родитель + загрузчик), чтобы
        // buildClasspath видел все библиотеки. Также пишем launch.json.
        const QString vjPath =
            QDir(m_dataDir).filePath(QStringLiteral("versions/") + m_versionId + QStringLiteral(".json"));
        if (!m_vi.isEmpty()) {
            QFile vj(vjPath);
            if (vj.open(QIODevice::WriteOnly | QIODevice::Truncate))
                vj.write(QJsonDocument(m_vi).toJson(QJsonDocument::Indented));
        }

        QJsonObject info;
        info.insert(QStringLiteral("mainClass"), m_mainClass);
        info.insert(QStringLiteral("loaderJar"), m_loaderJar);
        info.insert(QStringLiteral("assetIndex"), m_assetIndexId);
        info.insert(QStringLiteral("clientJar"), m_clientJar);
        info.insert(QStringLiteral("requiredJavaMajor"), m_requiredJavaMajor);
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

QString MinecraftInstaller::findJava(int requiredMajor)
{
    // Кэш предыдущего поиска (если мы сами скачали JRE). Проверяем версию —
    // кэш предыдущей Major-версии не должен использоваться для другой игры.
    const auto usableFor = [&](const QString &p) {
        if (p.isEmpty() || !QFileInfo::exists(p))
            return false;
        const int major = javaMajorVersion(p).toInt();
        if (major <= 0)
            return false;
        return requiredMajor <= 0 || major >= requiredMajor;
    };

    const QString cachedPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                   .filePath(QStringLiteral("runtime/java-path.txt"));
    QFile cf(cachedPath);
    if (cf.open(QIODevice::ReadOnly)) {
        const QString p = QString::fromUtf8(cf.readAll()).trimmed();
        if (usableFor(p))
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

    // Выбираем подходящую Java. Если требуется конкретный мажор — точное
    // совпадение, иначе ближайшая версия не ниже требуемой.
    QString best;
    int bestMajor = -1;
    for (const QString &c : candidates) {
        const int major = javaMajorVersion(c).toInt();
        if (major <= 0)
            continue;
        if (requiredMajor > 0) {
            if (major < requiredMajor)
                continue;
            if (major == requiredMajor)
                return c;
            if (bestMajor < 0 || major < bestMajor) {
                best = c;
                bestMajor = major;
            }
        } else if (major > bestMajor) {
            best = c;
            bestMajor = major;
        }
    }
    return best;
}

void MinecraftInstaller::downloadJre(const QString &dataDir, int major,
                                     const std::function<void(bool, const QString &)> &done)
{
    const QString runtimeDir = QDir(dataDir).filePath(QStringLiteral("runtime"));
    QDir().mkpath(runtimeDir);
    const QString zipName = QStringLiteral("jre%1.zip").arg(major);
    const QString zipPath = QDir(runtimeDir).filePath(zipName);
    setProgress(QStringLiteral("java"), 0, 1, 0, 0, zipName);
    downloadFile(QUrl(jreUrlForMajor(major)), zipPath, [this, runtimeDir, zipPath, done](bool ok, const QString &err) {
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

void MinecraftInstaller::ensureJava(const QString &versionId, const QString &dataDir,
                                    const std::function<void(bool, const QString &)> &done)
{
    const int required = requiredJavaMajorFromFiles(dataDir, versionId);
    const QString found = findJava(required);
    if (!found.isEmpty()) {
        m_javaPath = found;
        done(true, QString());
        return;
    }
    downloadJre(dataDir, required, done);
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
    m_requiredJavaMajor = info.value(QStringLiteral("requiredJavaMajor")).toInt(0);
    if (m_requiredJavaMajor <= 0)
        m_requiredJavaMajor = requiredJavaMajorFromFiles(dataDir, versionId);
    return !m_mainClass.isEmpty() && QFileInfo::exists(m_clientJar);
}

bool MinecraftInstaller::buildClasspath(const QString &versionId, const QString &dataDir)
{
    m_classpath.clear();
    m_classpath << m_clientJar;

    // Библиотеки из version JSON (только artifact, без natives)
    const QString vjPath = QDir(dataDir).filePath(QStringLiteral("versions/") + versionId + QStringLiteral(".json"));
    const QJsonObject vi = readJsonFile(vjPath, nullptr).object();
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
        m_javaPath = findJava(m_requiredJavaMajor);
    if (m_javaPath.isEmpty())
        return false;

    QStringList args;
    args << QStringLiteral("-Xmx2G") << QStringLiteral("-Xms512M");
    args << QStringLiteral("-Djava.library.path=%1").arg(m_nativesDir);
    args << QStringLiteral("-cp") << m_classpath.join(QDir::listSeparator());
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
        m_javaPath = findJava(m_requiredJavaMajor);
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
