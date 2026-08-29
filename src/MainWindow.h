#pragma once

#include <QMainWindow>

class QComboBox;
class QLabel;
class QPushButton;
class QTimer;

// Главное окно лаунчера EnderForge.
// Пока это только интерфейс: выбор версии (список пуст) и кнопка «Запустить».
class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    // Построение интерфейса
    void buildTopBar();
    void buildSidebar();
    void buildLaunchPanel();
    void buildStatusBar();

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
    QLabel *m_toast = nullptr;
    QTimer *m_toastTimer = nullptr;
};
