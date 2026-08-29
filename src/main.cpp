#include "MainWindow.h"
#include "VersionManager.h"
#include "pixelart.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QTextStream>
#include <QTimer>

namespace {

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
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("EnderForge"));
    app.setApplicationDisplayName(QStringLiteral("EnderForge — Minecraft Launcher"));
    app.setOrganizationName(QStringLiteral("EnderForge"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));

    // Аргументы командной строки:
    //   --screenshot <файл.png>  — сохранить скриншот окна и выйти (для CI)
    //   --manifest <файл.json>   — список версий из локального манифеста (офлайн/тесты)
    //   --list-versions          — вывести загруженные версии и выйти
    QString screenshotPath;
    QString manifestPath;
    bool listVersions = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            screenshotPath = QString::fromLocal8Bit(argv[++i]);
        else if (qstrcmp(argv[i], "--manifest") == 0 && i + 1 < argc)
            manifestPath = QString::fromLocal8Bit(argv[++i]);
        else if (qstrcmp(argv[i], "--list-versions") == 0)
            listVersions = true;
    }

    // Пиксельный шрифт для заголовков
    const QString fontPath = findResourcePath(QStringLiteral("resources/fonts/PressStart2P-Regular.ttf"));
    if (!fontPath.isEmpty())
        QFontDatabase::addApplicationFont(fontPath);

    // Тема
    const QString qssPath = findResourcePath(QStringLiteral("resources/theme.qss"));
    const QString qss = loadTextFile(qssPath);
    if (!qss.isEmpty())
        app.setStyleSheet(qss);

    MainWindow window;
    window.show();

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

    if (!manifestPath.isEmpty()) {
        // Офлайн-режим: подставляем локальный манифест вместо загрузки из сети
        QTimer::singleShot(0, &window, [&]() {
            window.loadVersionsFromFile(manifestPath);
        });
    }

    if (!screenshotPath.isEmpty()) {
        // Ждём завершения первой загрузки списка версий (или таймаут),
        // чтобы скриншот отражал актуальное состояние интерфейса.
        window.onVersionsRefreshFinished = [&]() {
            QTimer::singleShot(120, &window, [&]() {
                window.grab().save(screenshotPath);
                app.quit();
            });
        };
        QTimer::singleShot(8000, &app, [&]() {
            window.grab().save(screenshotPath);
            app.quit();
        });
    }

    return app.exec();
}
