#pragma once

#include "ProfileStore.h"

#include <QMainWindow>

#include <functional>

class AddProfileDialog;
class MinecraftInstaller;
class ProfileStore;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTimer;
class VersionManager;

// Главное окно лаунчера EnderForge.
// Слева — библиотека профилей (как игры в Steam), справа — детали
// выбранного профиля и запуск. Профиль = версия + загрузчик; игру можно
// скачать сразу или просто сохранить профиль, а запуск делает полное
// скачивание (клиент, библиотеки, ресурсы, natives) и старт Java.
class MainWindow : public QMainWindow
{
public:
    // dataDir — каталог данных (профили, клиенты); пусто = стандартный путь приложения
    explicit MainWindow(const QString &dataDir = QString(), QWidget *parent = nullptr);

    // Начать загрузку списка версий (используется и при старте, и в --screenshot)
    void startVersionRefresh();

    // Загрузить список версий из локального манифеста (офлайн/тесты)
    void loadVersionsFromFile(const QString &path);

    // Вызывается, когда попытка загрузки списка версий завершена (успех или ошибка).
    // Используется скриншот-режимом (--screenshot) и --list-versions.
    std::function<void()> onVersionsRefreshFinished;

    // Доступ к менеджеру версий (для --list-versions)
    VersionManager *versionManager() const { return m_versions; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    // Построение интерфейса
    void buildUi();
    void buildTopBar();
    void buildSidebar();
    void buildLaunchPanel();
    void buildStatusBar();

    // Создание VersionManager/MinecraftInstaller — откладывается до старта
    // event loop: конструкторы этих классов создают QNetworkAccessManager,
    // который на Windows может блокировать до показа окна.
    void initializeBackend();

    // Профили
    void refreshProfileList();
    void selectProfile(const QString &name);
    void onAddProfileClicked();
    void startDownload(const GameProfile &profile, bool launchAfterInstall);
    void onProfileListClicked();

    // Запуск
    void launchProfile(const GameProfile &profile);
    QString gameDirFor(const GameProfile &profile) const;

    // Список версий
    void onVersionsLoaded();
    void onVersionsFailed();

    // Действия
    void onLaunchClicked();
    void onSettingsClicked();

    // Уведомление-«тост»
    void showToast(const QString &message);
    void updateMainPanel();

    QWidget *m_central = nullptr;
    QWidget *m_topBar = nullptr;
    QWidget *m_sidebar = nullptr;
    QWidget *m_launchPanel = nullptr;
    QWidget *m_statusBar = nullptr;

    QLabel *m_avatarLabel = nullptr;
    QListWidget *m_profileList = nullptr;
    QPushButton *m_addProfileButton = nullptr;

    QLabel *m_profileNameLabel = nullptr;
    QLabel *m_profileDetailsLabel = nullptr;
    QLabel *m_profileStatusLabel = nullptr;
    QPushButton *m_downloadButton = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_launchButton = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_statusRight = nullptr;
    QLabel *m_toast = nullptr;
    QTimer *m_toastTimer = nullptr;

    VersionManager *m_versions = nullptr;
    ProfileStore *m_store = nullptr;
    MinecraftInstaller *m_installer = nullptr;

    QString m_dataDir;
    QString m_selectedProfile;
    bool m_manualRefresh = false;
    bool m_launchAfterInstall = false;  // запустить игру после установки
};
