#include "MainWindow.h"

#include "VersionManager.h"
#include "pixelart.h"

#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr const char *kPlaceholderLoading = "— загрузка версий…";
constexpr const char *kPlaceholderEmpty = "— версии недоступны —";

QStandardItem *makeSectionItem(const QString &text)
{
    auto *item = new QStandardItem(text);
    item->setFlags(Qt::NoItemFlags); // не выбирается
    item->setForeground(QBrush(QColor(0x8b, 0x98, 0xa8)));
    item->setSelectable(false);
    return item;
}

QWidget *makeCard(const QString &title, QWidget *content)
{
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

    // Список версий
    m_versions = new VersionManager(this);
    m_versions->onVersionsLoaded = [this]() { populateVersions(); };
    m_versions->onLoadFailed = [this](const QString &) { updateVersionUi(); };
    m_versions->onRefreshFinished = [this]() {
        m_refreshButton->setEnabled(true);
        m_refreshButton->setText(QStringLiteral("\u21bb"));
        if (onVersionsRefreshFinished)
            onVersionsRefreshFinished();
    };
    connect(m_versionSelect, &QComboBox::currentIndexChanged, this, [this](int) {
        m_launchButton->setEnabled(!m_versionSelect->currentData().toString().isEmpty());
    });

    // При старте сразу загружаем список версий
    startVersionRefresh();
}

void MainWindow::startVersionRefresh()
{
    m_refreshButton->setEnabled(false);
    m_refreshButton->setText(QStringLiteral("\u2026"));
    if (m_versionModel && m_versionModel->rowCount() == 0)
        m_hintLabel->setText(QStringLiteral("Загрузка списка версий…"));
    m_versions->refresh();
}

void MainWindow::loadVersionsFromFile(const QString &path)
{
    m_refreshButton->setEnabled(false);
    m_refreshButton->setText(QStringLiteral("\u2026"));
    m_versions->loadFromFile(path);
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

    auto *versionBadge = new QLabel(QStringLiteral("v0.2.0-alpha"), m_topBar);
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

    m_versionModel = new QStandardItemModel(this);
    m_versionModel->appendRow(new QStandardItem(QString::fromLatin1(kPlaceholderLoading)));

    m_versionSelect = new QComboBox(m_launchPanel);
    m_versionSelect->setObjectName(QStringLiteral("versionSelect"));
    m_versionSelect->setFixedHeight(44);
    m_versionSelect->setModel(m_versionModel);
    versionRow->addWidget(m_versionSelect, 1);

    m_refreshButton = new QPushButton(QStringLiteral("\u21bb"), m_launchPanel);
    m_refreshButton->setObjectName(QStringLiteral("refreshButton"));
    m_refreshButton->setToolTip(QStringLiteral("Обновить список версий"));
    m_refreshButton->setFixedSize(44, 44);
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    versionRow->addWidget(m_refreshButton);

    layout->addLayout(versionRow);

    // Кнопка запуска
    m_launchButton = new QPushButton(QStringLiteral("\u25b6   ЗАПУСТИТЬ"), m_launchPanel);
    m_launchButton->setObjectName(QStringLiteral("launchButton"));
    m_launchButton->setFixedHeight(56);
    m_launchButton->setCursor(Qt::PointingHandCursor);
    m_launchButton->setEnabled(false); // станет активной, когда выберем версию
    connect(m_launchButton, &QPushButton::clicked, this, &MainWindow::onLaunchClicked);
    layout->addWidget(m_launchButton);

    m_hintLabel = new QLabel(QStringLiteral("Загрузка списка версий…"), m_launchPanel);
    m_hintLabel->setObjectName(QStringLiteral("hint"));
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_hintLabel);

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

    m_statusRight = new QLabel(QStringLiteral("загрузка списка версий…"), m_statusBar);
    m_statusRight->setObjectName(QStringLiteral("statusText"));
    layout->addWidget(m_statusRight);
}

// ---------- Список версий ----------

void MainWindow::populateVersions()
{
    m_versionModel->clear();

    const QVector<VersionInfo> versions = m_versions->versions();
    if (versions.isEmpty()) {
        updateVersionUi();
        return;
    }

    const QString latestRelease = m_versions->latestRelease();
    const QString latestSnapshot = m_versions->latestSnapshot();

    // --- "Последние версии" ---
    m_versionModel->appendRow(makeSectionItem(QStringLiteral("ПОСЛЕДНИЕ ВЕРСИИ")));
    const auto addLatest = [&](const QString &id, const QString &suffix) {
        if (id.isEmpty())
            return;
        auto *item = new QStandardItem(QStringLiteral("%1  %2").arg(id, suffix));
        item->setData(id, Qt::UserRole);
        item->setForeground(QBrush(QColor(0x3d, 0xdc, 0x68)));
        m_versionModel->appendRow(item);
    };
    addLatest(latestRelease, QStringLiteral("\u2605  последний релиз"));
    addLatest(latestSnapshot, QStringLiteral("\u2606  снапшот"));

    // --- Группы ---
    const auto appendGroup = [&](const QString &title, const QStringList &types,
                                 const QColor &color) {
        QVector<QStandardItem *> items;
        for (const VersionInfo &v : versions) {
            if (!types.contains(v.type))
                continue;
            auto *item = new QStandardItem(v.id);
            item->setData(v.id, Qt::UserRole);
            item->setData(v.type, Qt::UserRole + 1);
            item->setForeground(QBrush(color));
            item->setToolTip(QStringLiteral("%1 · %2")
                                 .arg(v.type, v.releaseTime.date().toString(Qt::ISODate)));
            items.append(item);
        }
        if (items.isEmpty())
            return;
        m_versionModel->appendRow(makeSectionItem(title));
        for (QStandardItem *item : items)
            m_versionModel->appendRow(item);
    };

    appendGroup(QStringLiteral("РЕЛИЗЫ"), { QStringLiteral("release") },
                QColor(0xe8, 0xee, 0xf5));
    appendGroup(QStringLiteral("СНАПШОТЫ"), { QStringLiteral("snapshot") },
                QColor(0x9a, 0xa6, 0xb5));
    appendGroup(QStringLiteral("СТАРЫЕ БЕТЫ И АЛЬФЫ"),
                { QStringLiteral("old_beta"), QStringLiteral("old_alpha") },
                QColor(0x7a, 0x86, 0x96));

    // По умолчанию выбираем последний релиз
    for (int i = 0; i < m_versionModel->rowCount(); ++i) {
        QStandardItem *item = m_versionModel->item(i);
        if (item->data(Qt::UserRole).toString() == latestRelease) {
            m_versionSelect->setCurrentIndex(i);
            break;
        }
    }

    updateVersionUi();
}

void MainWindow::updateVersionUi()
{
    const bool loaded = m_versions->isLoaded();
    const int count = m_versions->versions().size();

    if (loaded) {
        m_hintLabel->setText(
            QStringLiteral("Доступно версий: %1 · запуск игры появится в следующем обновлении")
                .arg(count));
        m_statusRight->setText(QStringLiteral("версий: %1 · последний релиз: %2")
                                   .arg(count)
                                   .arg(m_versions->latestRelease()));
        if (m_manualRefresh)
            showToast(QStringLiteral("Список версий обновлён (%1 шт.)").arg(count));
    } else {
        // Место «загрузки» занято ошибкой
        m_versionModel->clear();
        m_versionModel->appendRow(new QStandardItem(QString::fromLatin1(kPlaceholderEmpty)));
        m_hintLabel->setText(
            QStringLiteral("Не удалось загрузить список версий. Проверьте подключение к интернету."));
        m_statusRight->setText(QStringLiteral("список версий недоступен"));
        if (m_manualRefresh)
            showToast(QStringLiteral("Не удалось обновить список версий"));
    }
    m_manualRefresh = false;
}

// ---------- Действия ----------

void MainWindow::onLaunchClicked()
{
    const QString version = m_versionSelect->currentData().toString();
    if (version.isEmpty()) {
        showToast(QStringLiteral("Сначала выберите версию"));
        return;
    }
    // TODO: загрузка клиента и запуск игры
    showToast(QStringLiteral("Запускаю %1… (скоро!)").arg(version));
}

void MainWindow::onRefreshClicked()
{
    m_manualRefresh = true;
    startVersionRefresh();
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
