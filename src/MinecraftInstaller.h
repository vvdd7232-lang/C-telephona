#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Прогресс установки: этап, доля шагов, байты текущего файла.
struct InstallProgress {
    QString stage;             // "клиент", "библиотеки", "ресурсы", "java"…
    int completedSteps = 0;
    int totalSteps = 0;
    qint64 bytesGot = 0;
    qint64 bytesTotal = 0;
    QString currentFile;       // короткое имя текущего файла
};

// Полная установка и запуск Minecraft.
//
// Установка (install):
//   1. version JSON (с учётом inheritsFrom)      -> dataDir/versions/<id>.json
//   2. клиентский jar                            -> dataDir/versions/<id>/<id>.jar
//   3. все библиотеки по правилам (windows x64)  -> dataDir/libraries/<path>
//   4. asset index + все объекты ресурсов        -> dataDir/assets/…
//   5. natives (извлечение .dll из jar)          -> dataDir/versions/<id>/natives
//   6. загрузчик fabric/quilt (jar + mainClass)  -> dataDir/libraries/…
//
// Запуск (launchGame): поиск Java нужной версии (или скачивание Temurin JRE),
// сборка classpath, запуск клиента через QProcess с аргументами официального
// лаунчера (офлайн-сессия: singleplayer работает).
//
// Замечание: без Q_OBJECT (собирается без moc) — колбэки через std::function.
class MinecraftInstaller : public QObject
{
public:
    explicit MinecraftInstaller(QObject *parent = nullptr);

    // --- Установка -------------------------------------------------------
    // urlResolver(id) возвращает URL version JSON по id (для родительских версий).
    void install(const QString &versionId, const QString &loader,
                 const QString &dataDir, const QString &gameDir,
                 const std::function<QString(const QString &)> &urlResolver);

    bool isBusy() const { return m_busy; }
    void stop();

    // Колбэки установки
    std::function<void(const InstallProgress &)> onProgress;
    std::function<void(bool ok, const QString &error)> onFinished;

    // --- Java ------------------------------------------------------------
    // Найти установленную Java (JAVA_HOME, стандартные пути, where java).
    // requiredMajor > 0 — искать Java этой мажорной версии (или ближайшую
    // доступную не ниже). Вернёт путь к java.exe или пустую строку.
    static QString findJava(int requiredMajor = 0);

    // Убедиться, что Java нужной версии есть: если подходящей Java нет —
    // скачать Temurin JRE нужного мажора и распаковать в dataDir/runtime.
    // done(ok, error).
    void ensureJava(const QString &versionId, const QString &dataDir,
                    const std::function<void(bool, const QString &)> &done);

    // --- Запуск ----------------------------------------------------------
    // Запустить установленный профиль. Требует завершённой install().
    bool launchGame(const QString &versionId, const QString &dataDir,
                    const QString &gameDir);

    void stopGame();
    bool isGameRunning() const;

    std::function<void()> onGameStarted;
    std::function<void(int exitCode)> onGameFinished;
    std::function<void(const QString &)> onGameOutput;

    QString javaPath() const { return m_javaPath; }
    QString clientJar() const { return m_clientJar; }

private:
    // --- установка ---
    void downloadFile(const QUrl &url, const QString &destPath,
                      std::function<void(bool, const QString &)> done,
                      qint64 expectedSize = 0);
    void downloadVersionJson();
    void downloadClient();
    void downloadLibraries();
    void downloadNextLibrary();
    void downloadAssetIndex();
    void downloadAssets();
    void downloadNextAsset();
    void extractNatives();
    void extractNextNative();
    void resolveLoader();
    void downloadLoaderLibraries();
    void downloadNextLoaderLibrary();
    void determineJavaMajor();
    void finish(bool ok, const QString &err);
    void setProgress(const QString &stage, int done, int total,
                     qint64 got = 0, qint64 all = 0, const QString &file = QString());

    // --- запуск ---
    bool loadLaunchInfo(const QString &versionId, const QString &dataDir);
    bool buildClasspath(const QString &versionId, const QString &dataDir);
    bool startProcess();

    // --- java ---
    static QString javaMajorVersion(const QString &javaExe);
    void downloadJre(const QString &dataDir, int major,
                     const std::function<void(bool, const QString &)> &done);
    void extractZipWithTar(const QString &zipPath, const QString &destDir,
                           std::function<void(bool)> done);

    // данные
    QString m_versionId;
    QString m_loader;
    QString m_dataDir;
    QString m_gameDir;
    std::function<QString(const QString &)> m_urlResolver;
    bool m_busy = false;
    bool m_cancelled = false;

    QString m_currentStage;      // текущий этап (для downloadProgress)
    int m_currentDone = 0;
    int m_currentTotal = 0;

    QJsonObject m_vi;              // version JSON (объединённый с родителем)
    QString m_assetIndexId;
    QString m_mainClass;
    QString m_clientJar;
    QString m_javaPath;
    QStringList m_classpath;       // jar'ы для -cp (клиент + библиотеки + загрузчик)
    QStringList m_libraryJars;     // библиотеки (не natives) — абсолютные пути
    QStringList m_nativeJars;      // natives-джарники для распаковки
    QString m_nativesDir;
    QString m_assetsRoot;          // dataDir/assets
    QString m_loaderJar;           // fabric/quilt loader jar (абсолютный путь)
    int m_requiredJavaMajor = 17;  // требуемая версия Java

    // Очередь последовательных загрузок (не лямбда на стеке — иначе UAF)
    struct FileTask {
        QString url;
        QString dest;
        qint64 expectedSize = 0;
    };
    QList<FileTask> m_fileTasks;
    int m_taskIndex = 0;
    int m_nativeIndex = 0;

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    QProcess *m_gameProcess = nullptr;
};
