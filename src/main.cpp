#include "MainWindow.h"
#include "pixelart.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
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
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    // Скрытый режим: --screenshot <файл.png> — сохранить скриншот окна и выйти.
    // Полезно для CI и проверки интерфейса без дисплея.
    QString screenshotPath;
    for (int i = 1; i < argc - 1; ++i) {
        if (qstrcmp(argv[i], "--screenshot") == 0)
            screenshotPath = QString::fromLocal8Bit(argv[i + 1]);
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

    if (!screenshotPath.isEmpty()) {
        // Ждём пару кадров, чтобы всё отрисовалось, затем снимаем скриншот
        QTimer::singleShot(150, &window, [&]() {
            window.grab().save(screenshotPath);
            app.quit();
        });
    }

    return app.exec();
}
