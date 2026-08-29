#include "MainWindow.h"

#include "pixelart.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Цветовая схема (продублирована в resources/theme.qss)
constexpr const char *kAccentColor = "#3ddc68";

QWidget *makeCard(const QString &title, QWidget *content) {
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("card"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(12);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(titleLabel);

    layout->addWidget(content, 1);
    return card;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("EnderForge — Minecraft Launcher"));
    setMinimumSize(920, 560);
    resize(1000, 650);

    m_central = new QWidget(this);
    setCentralWidget(m_central);

    auto *root = new QVBoxLayout(m_central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildTopBar();
    buildSidebar();
    buildLaunchPanel();
    buildStatusBar();

    // Тело окна: сайдбар слева + панель запуска
    auto *body = new QWidget(m_central);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(m_sidebar);
    bodyLayout->addWidget(m_launchPanel, 1);

    root->addWidget(m_topBar);
    root->addWidget(body, 1);
    root->addWidget(m_statusBar);

    // Тост-уведомление (поверх всего)
    m_toast = new QLabel(this);
    m_toast->setObjectName(QStringLiteral("toast"));
    m_toast->setAlignment(Qt::AlignCenter);
    m_toast->hide();

    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    m_toastTimer->setInterval(2600);
    connect(m_toastTimer, &QTimer::timeout, m_toast, &QLabel::hide);

    // Иконка окна — травяной блок
    setWindowIcon(QIcon(grassBlockPixmap(6)));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (m_toast && !m_toast->isHidden()) {
        const int w = qMin(460, width() - 80);
        m_toast->setFixedWidth(w);
        m_toast->move((width() - w) / 2, height() - 96);
    }
}

// ---------- Верхняя панель ----------

void MainWindow::buildTopBar()
{
    m_topBar = new QWidget(m_central);
    m_topBar->setObjectName(QStringLiteral("topBar"));
    m_topBar->setFixedHeight(64);

    auto *layout = new QHBoxLayout(m_topBar);
    layout->setContentsMargins(18, 0, 18, 0);
    layout->setSpacing(12);

    // Логотип + название
    auto *logo = new QLabel(m_topBar);
    logo->setPixmap(grassBlockPixmap(5)); // 40x40
    layout->addWidget(logo);

    auto *brandBox = new QVBoxLayout;
    brandBox->setSpacing(2);
    auto *brandName = new QLabel(QStringLiteral("ENDERFORGE"), m_topBar);
    brandName->setObjectName(QStringLiteral("brandName"));
    brandBox->addWidget(brandName);
    auto *brandTagline = new QLabel(QStringLiteral("MINECRAFT LAUNCHER"), m_topBar);
    brandTagline->setObjectName(QStringLiteral("brandTagline"));
    brandBox->addWidget(brandTagline);
    layout->addLayout(brandBox);

    layout->addStretch(1);

    auto *versionBadge = new QLabel(QStringLiteral("v0.1.0-alpha"), m_topBar);
    versionBadge->setObjectName(QStringLiteral("versionBadge"));
    layout->addWidget(versionBadge);

    auto *settingsBtn = new QPushButton(QStringLiteral("\u2699"), m_topBar); // шестерёнка
    settingsBtn->setObjectName(QStringLiteral("settingsButton"));
    settingsBtn->setToolTip(QStringLiteral("Настройки"));
    settingsBtn->setFixedSize(38, 38);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    layout->addWidget(settingsBtn);
}

// ---------- Боковая панель ----------

void MainWindow::buildSidebar()
{
    m_sidebar = new QWidget(m_central);
    m_sidebar->setObjectName(QStringLiteral("sidebar"));
    m_sidebar->setFixedWidth(280);

    auto *layout = new QVBoxLayout(m_sidebar);
    layout->setContentsMargins(18, 18, 0, 18);
    layout->setSpacing(14);

    // Карточка аккаунта
    auto *accountRow = new QWidget;
    auto *accountLayout = new QHBoxLayout(accountRow);
    accountLayout->setContentsMargins(0, 0, 0, 0);
    accountLayout->setSpacing(12);

    m_avatarLabel = new QLabel(accountRow);
    m_avatarLabel->setPixmap(steveHeadPixmap(6)); // 48x48
    m_avatarLabel->setObjectName(QStringLiteral("avatar"));
    accountLayout->addWidget(m_avatarLabel);

    auto *accountInfo = new QVBoxLayout;
    accountInfo->setSpacing(3);
    auto *accountName = new QLabel(QStringLiteral("Не авторизован"), accountRow);
    accountName->setObjectName(QStringLiteral("accountName"));
    accountInfo->addWidget(accountName);
    auto *accountStatus = new QLabel(QStringLiteral("\u25cf  оффлайн"), accountRow);
    accountStatus->setObjectName(QStringLiteral("accountStatus"));
    accountInfo->addWidget(accountStatus);
    accountLayout->addLayout(accountInfo);
    accountLayout->addStretch(1);

    layout->addWidget(makeCard(QStringLiteral("АККАУНТ"), accountRow));

    // Карточка новостей
    auto *newsBody = new QLabel(
        QStringLiteral("Здесь скоро появятся новости и анонсы обновлений."), m_sidebar);
    newsBody->setObjectName(QStringLiteral("newsBody"));
    newsBody->setWordWrap(true);
    newsBody->setAlignment(Qt::AlignCenter);
    layout->addWidget(makeCard(QStringLiteral("НОВОСТИ"), newsBody), 1);
}

// ---------- Карточка запуска ----------

void MainWindow::buildLaunchPanel()
{
    m_launchPanel = new QFrame(m_central);
    m_launchPanel->setObjectName(QStringLiteral("launchPanel"));

    auto *layout = new QVBoxLayout(m_launchPanel);
    layout->setContentsMargins(48, 42, 48, 42);
    layout->setSpacing(20);
    layout->addStretch(1);

    auto *sectionTitle = new QLabel(QStringLiteral("ЗАПУСК ИГРЫ"), m_launchPanel);
    sectionTitle->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(sectionTitle, 0, Qt::AlignHCenter);

    auto *sectionSub = new QLabel(
        QStringLiteral("Выберите версию и вперёд — в блоки!"), m_launchPanel);
    sectionSub->setObjectName(QStringLiteral("sectionSub"));
    layout->addWidget(sectionSub, 0, Qt::AlignHCenter);

    // Выбор версии
    auto *versionLabel = new QLabel(QStringLiteral("ВЕРСИЯ ИГРЫ"), m_launchPanel);
    versionLabel->setObjectName(QStringLiteral("versionLabel"));
    layout->addWidget(versionLabel);

    auto *versionRow = new QHBoxLayout;
    versionRow->setSpacing(10);

    m_versionSelect = new QComboBox(m_launchPanel);
    m_versionSelect->setObjectName(QStringLiteral("versionSelect"));
    m_versionSelect->setFixedHeight(44);
    // Пока список версий пуст — только заглушка
    m_versionSelect->addItem(QStringLiteral("— версии пока не добавлены —"));
    versionRow->addWidget(m_versionSelect, 1);

    auto *refreshBtn = new QPushButton(QStringLiteral("\u21bb"), m_launchPanel);
    refreshBtn->setObjectName(QStringLiteral("refreshButton"));
    refreshBtn->setToolTip(QStringLiteral("Обновить список версий"));
    refreshBtn->setFixedSize(44, 44);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    versionRow->addWidget(refreshBtn);

    layout->addLayout(versionRow);

    // Кнопка запуска
    auto *launchBtn = new QPushButton(QStringLiteral("\u25b6   ЗАПУСТИТЬ"), m_launchPanel);
    launchBtn->setObjectName(QStringLiteral("launchButton"));
    launchBtn->setFixedHeight(56);
    launchBtn->setCursor(Qt::PointingHandCursor);
    connect(launchBtn, &QPushButton::clicked, this, &MainWindow::onLaunchClicked);
    layout->addWidget(launchBtn);

    auto *hint = new QLabel(
        QStringLiteral("Список версий пока пуст — загрузка появится в следующем обновлении лаунчера."),
        m_launchPanel);
    hint->setObjectName(QStringLiteral("hint"));
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);

    layout->addStretch(1);
}

// ---------- Нижняя панель ----------

void MainWindow::buildStatusBar()
{
    m_statusBar = new QWidget(m_central);
    m_statusBar->setObjectName(QStringLiteral("statusBar"));
    m_statusBar->setFixedHeight(36);

    auto *layout = new QHBoxLayout(m_statusBar);
    layout->setContentsMargins(18, 0, 18, 0);

    auto *left = new QLabel(QStringLiteral("\u00a9 2026 EnderForge"), m_statusBar);
    left->setObjectName(QStringLiteral("statusText"));
    layout->addWidget(left);

    layout->addStretch(1);

    auto *right = new QLabel(QStringLiteral("интерфейс v0.1 — запуск пока не подключён"), m_statusBar);
    right->setObjectName(QStringLiteral("statusText"));
    layout->addWidget(right);
}

// ---------- Действия ----------

void MainWindow::onLaunchClicked()
{
    const QString version = m_versionSelect->currentData().toString();
    if (version.isEmpty()) {
        showToast(QStringLiteral("Версии пока не добавлены — скоро появятся!"));
        return;
    }
    // TODO: загрузка клиента и запуск игры
    showToast(QStringLiteral("Запускаю %1… (скоро!)").arg(version));
}

void MainWindow::onRefreshClicked()
{
    // TODO: запрос списка версий у Mojang (versions.json)
    showToast(QStringLiteral("Список версий пуст — обновлять пока нечего"));
}

void MainWindow::onSettingsClicked()
{
    // TODO: окно настроек (память, путь к игре, учётная запись)
    showToast(QStringLiteral("Настройки появятся позже"));
}

// ---------- Тост ----------

void MainWindow::showToast(const QString &message)
{
    m_toast->setText(message);
    m_toast->adjustSize();

    const int w = qMin(460, width() - 80);
    m_toast->setFixedWidth(w);
    m_toast->move((width() - w) / 2, height() - 96);
    m_toast->show();
    m_toast->raise();

    m_toastTimer->start();
}
