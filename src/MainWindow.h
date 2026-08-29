#pragma once

#include <QMainWindow>

#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QStandardItemModel;
class QTimer;
class VersionManager;

// Главное окно лаунчера EnderForge.
// Пока это только интерфейс: выбор версии (список из манифеста Mojang)
// и кнопка «Запустить» (запуск клиента — в следующем обновлении).
class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

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
    void buildTopBar();
    void buildSidebar();
    void buildLaunchPanel();
    void buildStatusBar();

    // Список версий
    void populateVersions();
    void updateVersionUi();

    // Действия
    void onLaunchClicked();
    void onRefreshClicked();
    void onSettingsClicked();

    // Уведомление-«тост»
    void showToast(const QString &message);

    QWidget *m_central = nullptr;
    QWidget *m_topBar = nullptr;
    QWidget *m_sidebar = nullptr;
    QWidget *m_launchPanel = nullptr;
    QWidget *m_statusBar = nullptr;

    QLabel *m_avatarLabel = nullptr;
    QComboBox *m_versionSelect = nullptr;
    QStandardItemModel *m_versionModel = nullptr;
    QPushButton *m_launchButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_statusRight = nullptr;
    QLabel *m_toast = nullptr;
    QTimer *m_toastTimer = nullptr;

    VersionManager *m_versions = nullptr;
    bool m_manualRefresh = false;
};
