#include "AddProfileDialog.h"
#include "GameDownloader.h"
#include "MainWindow.h"
#include "VersionManager.h"
#include "pixelart.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageLogContext>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

namespace {

// Простой лог-файл: EnderForge.log рядом с exe (или в %TEMP%). Пишет сообщения
// Qt (qDebug/qWarning/...) и этапы запуска — помогает диагностировать,
// на каком шаге приложение застревает на машине пользователя.
QString g_logPath;
bool g_logInit = false;

void appendLogLine(const QString &text)
{
    if (g_logPath.isEmpty())
        return;
    QFile f(g_logPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
           << QLatin1Char(' ') << text << QLatin1Char('\n');
    }
}

void logToFile(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    switch (type) {
    case QtDebugMsg:    appendLogLine(QStringLiteral("[debug] ") + msg); break;
    case QtInfoMsg:     appendLogLine(QStringLiteral("[info]  ") + msg); break;
    case QtWarningMsg:  appendLogLine(QStringLiteral("[warn]  ") + msg); break;
    case QtCriticalMsg: appendLogLine(QStringLiteral("[crit]  ") + msg); break;
    case QtFatalMsg:    appendLogLine(QStringLiteral("[fatal] ") + msg); break;
    }
}

void initLog()
{
    if (g_logInit)
        return;
    g_logInit = true;
    g_logPath = QDir::temp().filePath(QStringLiteral("EnderForge.log"));
    appendLogLine(QStringLiteral("=== EnderForge startup ==="));
    qInstallMessageHandler(logToFile);
}

void stage(const QString &s)
{
    appendLogLine(QStringLiteral("[stage] ") + s);
}

// Поиск файла ресурсов: сначала рядом с бинарником, затем в текущей папке,
// затем в исходниках проекта (задаётся через ENDERFORGE_RESOURCE_DIR).
QString findResourcePath(const QString &relative)
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QLatin1Char('/') + relative,
        QDir::currentPath() + QLatin1Char('/') + relative,
#ifdef ENDERFORGE_RESOURCE_DIR
        QStringLiteral(ENDERFORGE_RESOURCE_DIR) + QLatin1Char('/') + relative,
#endif
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

QString loadTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

} // namespace

int main(int argc, char *argv[])
{
    // Сразу отключаем Qt-опрос системного прокси (WPAD): на Windows эта
    // синхронная инициализация может блокировать запуск на несколько минут.
    // Делаем до создания QApplication, чтобы QtNetwork не успел её запустить.
    qputenv("QT_NO_SYSTEMPROXY", "1");

    initLog();
    stage(QStringLiteral("before QApplication"));
    QApplication app(argc, argv);
    stage(QStringLiteral("after QApplication"));

    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    app.setApplicationName(QStringLiteral("EnderForge"));
    app.setApplicationDisplayName(QStringLiteral("EnderForge — Minecraft Launcher"));
    app.setOrganizationName(QStringLiteral("EnderForge"));
    app.setApplicationVersion(QStringLiteral("0.5.0"));

    // Аргументы командной строки:
    //   --screenshot <файл.png>  — сохранить скриншот окна и выйти (для CI)
    //   --manifest <файл.json>   — список версий из локального манифеста (офлайн/тесты)
    //   --list-versions          — вывести загруженные версии и выйти
    //   --data-dir <путь>        — каталог данных (профили, клиенты)
    //   --download-test <url-версии.json> <выход.jar> — проверить скачивание и выйти
    //   --screenshot-dialog <файл.png> — скриншот диалога «Добавить профиль»
    QString screenshotPath;
    QString dialogScreenshotPath;
    QString manifestPath;
    QString dataDir;
    QString downloadTestUrl;
    QString downloadTestOut;
    bool listVersions = false;

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        auto next = [&]() -> QString {
            return (i + 1 < argc) ? QString::fromLocal8Bit(argv[++i]) : QString();
        };
        if (arg == QLatin1String("--screenshot"))
            screenshotPath = next();
        else if (arg == QLatin1String("--screenshot-dialog"))
            dialogScreenshotPath = next();
        else if (arg == QLatin1String("--manifest"))
            manifestPath = next();
        else if (arg == QLatin1String("--data-dir"))
            dataDir = next();
        else if (arg == QLatin1String("--list-versions"))
            listVersions = true;
        else if (arg == QLatin1String("--download-test")) {
            downloadTestUrl = next();
            downloadTestOut = next();
        }
    }

    if (dataDir.isEmpty())
        dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    // Пиксельный шрифт для заголовков
    const QString fontPath = findResourcePath(QStringLiteral("resources/fonts/PressStart2P-Regular.ttf"));
    if (!fontPath.isEmpty())
        QFontDatabase::addApplicationFont(fontPath);

    // Тема
    const QString qssPath = findResourcePath(QStringLiteral("resources/theme.qss"));
    const QString qss = loadTextFile(qssPath);
    if (!qss.isEmpty())
        app.setStyleSheet(qss);

    // --- Режим проверки скачивания (без окна) ---
    if (!downloadTestUrl.isEmpty()) {
        auto *downloader = new GameDownloader(&app);
        QTextStream out(stdout);
        downloader->onProgress = [&](qint64 got, qint64 total) {
            if (total > 0)
                out << "progress: " << got << "/" << total << "\n";
        };
        downloader->onFinished = [&](bool ok, const QString &error) {
            if (ok) {
                out << "DOWNLOAD OK size="
                    << QFileInfo(downloadTestOut).size() << "\n";
            } else {
                out << "DOWNLOAD FAIL: " << error << "\n";
            }
            out.flush();
            app.quit();
        };
        downloader->downloadClient(downloadTestUrl, downloadTestOut);
        return app.exec();
    }

    stage(QStringLiteral("before MainWindow ctor"));
    MainWindow window(dataDir);
    stage(QStringLiteral("after MainWindow ctor"));
    window.show();
    stage(QStringLiteral("after show()"));

    if (!manifestPath.isEmpty()) {
        // Офлайн-режим: подставляем локальный манифест вместо загрузки из сети
        QTimer::singleShot(0, &window, [&]() {
            window.loadVersionsFromFile(manifestPath);
        });
    }

    if (listVersions) {
        // Показываем список версий и выходим
        window.onVersionsRefreshFinished = [&]() {
            const auto versions = window.versionManager()->versions();
            QTextStream out(stdout);
            out << "latest release: " << window.versionManager()->latestRelease() << "\n";
            out << "latest snapshot: " << window.versionManager()->latestSnapshot() << "\n";
            out << "total: " << versions.size() << "\n";
            for (const VersionInfo &v : versions)
                out << v.id << "\t" << v.type << "\n";
            out.flush();
            app.quit();
        };
        QTimer::singleShot(12000, &app, &QCoreApplication::quit);
    }

    if (!screenshotPath.isEmpty() || !dialogScreenshotPath.isEmpty()) {
        // Скриншот диалога «Добавить профиль» (создаём на куче — диалог живёт
        // пока живёт окно, и таймер успевает сработать)
        auto shootDialog = [&]() {
            auto *dlg = new AddProfileDialog(window.versionManager(), &window);
            dlg->show();
            QApplication::processEvents(); // раскладка и polish
            dlg->grab().save(dialogScreenshotPath);
            app.quit();
        };

        // Ждём завершения первой загрузки списка версий (или таймаут)
        window.onVersionsRefreshFinished = [&]() {
            QTimer::singleShot(120, &window, [&]() {
                if (!screenshotPath.isEmpty())
                    window.grab().save(screenshotPath);
                if (!dialogScreenshotPath.isEmpty()) {
                    shootDialog();
                    return;
                }
                app.quit();
            });
        };
        QTimer::singleShot(8000, &app, [&]() {
            if (!screenshotPath.isEmpty())
                window.grab().save(screenshotPath);
            if (!dialogScreenshotPath.isEmpty()) {
                shootDialog();
                return;
            }
            app.quit();
        });
    }

    stage(QStringLiteral("before exec()"));
    const int rc = app.exec();
    stage(QStringLiteral("after exec() rc=%1").arg(rc));
    return rc;
}
