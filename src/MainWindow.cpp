#include "MainWindow.h"

#include "AddProfileDialog.h"
#include "MinecraftInstaller.h"
#include "ProfileStore.h"
#include "VersionManager.h"
#include "pixelart.h"

#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {

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

QPixmap statusDot(const QColor &color)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawEllipse(1, 1, 10, 10);
    return pm;
}

} // namespace

MainWindow::MainWindow(const QString &dataDir, QWidget *parent)
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

    // Каталог данных
    QString dir = dataDir;
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    m_store = new ProfileStore(dir, this);
    m_store->load();

    refreshProfileList();
    // Выбираем первый профиль, если он есть
    if (!m_store->profiles().isEmpty())
        selectProfile(m_store->profiles().first().name);

    // VersionManager и MinecraftInstaller создаём ЧЕРЕЗ МГНОВЕНИЕ после
    // показа окна: их конструкторы создают QNetworkAccessManager, который
    // на Windows может блокироваться (WPAD/DNS/TLS). Даём Qt отрисовать окно,
    // а только потом создаём сетевой бэкенд.
    QTimer::singleShot(300, this, &MainWindow::initializeBackend);
}

void MainWindow::initializeBackend()
{
    if (!m_versions)
        m_versions = new VersionManager(this);
    m_versions->onVersionsLoaded = [this]() { onVersionsLoaded(); };
    m_versions->onLoadFailed = [this](const QString &) { onVersionsFailed(); };
    m_versions->onRefreshFinished = [this]() {
        if (onVersionsRefreshFinished)
            onVersionsRefreshFinished();
    };

    if (!m_installer)
        m_installer = new MinecraftInstaller(this);
    m_installer->onProgress = [this](const InstallProgress &p) {
        const int percent = p.totalSteps > 0
            ? qBound(0, p.completedSteps * 100 / p.totalSteps, 100)
            : 0;
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(percent);
        m_progressBar->show();
        QString text = QStringLiteral("%1… %2/%3")
                           .arg(p.stage)
                           .arg(p.completedSteps)
                           .arg(p.totalSteps);
        if (!p.currentFile.isEmpty())
            text += QStringLiteral(" · %1").arg(p.currentFile);
        m_profileStatusLabel->setText(text);
    };
    m_installer->onFinished = [this](bool ok, const QString &error) {
        m_progressBar->hide();
        m_downloadButton->setEnabled(true);
        m_launchButton->setEnabled(true);

        GameProfile profile;
        bool found = false;
        for (const GameProfile &p : m_store->profiles()) {
            if (p.name == m_selectedProfile) {
                profile = p;
                found = true;
                break;
            }
        }
        if (!found)
            return;

        if (ok) {
            profile.downloaded = true;
            profile.downloadError.clear();
            profile.clientPath = m_installer->clientJar();
            m_store->updateProfile(profile);
            refreshProfileList();
            selectProfile(profile.name);

            if (m_launchAfterInstall) {
                m_launchAfterInstall = false;
                launchProfile(profile);
            } else {
                showToast(QStringLiteral("Игра «%1» установлена").arg(profile.name));
            }
        } else {
            profile.downloaded = false;
            profile.downloadError = error;
            m_store->updateProfile(profile);
            refreshProfileList();
            selectProfile(profile.name);
            m_launchAfterInstall = false;
            showToast(QStringLiteral("Не удалось установить: %1").arg(error));
        }
    };
    m_installer->onGameStarted = [this]() {
        showToast(QStringLiteral("Игра запущена!"));
        updateMainPanel();
    };
    m_installer->onGameFinished = [this](int code) {
        showToast(QStringLiteral("Игра завершена (код %1)").arg(code));
        updateMainPanel();
    };

    updateMainPanel();
    // Сетевую загрузку версий запускаем следующей итерацией event loop:
    // окно гарантированно отрисовано, даже если QtNetwork зависнет позже.
    QTimer::singleShot(0, this, [this]() { startVersionRefresh(); });
}

void MainWindow::startVersionRefresh()
{
    if (m_versions)
        m_versions->refresh();
}

void MainWindow::loadVersionsFromFile(const QString &path)
{
    if (!m_versions) {
        QTimer::singleShot(0, this, [this, path]() { loadVersionsFromFile(path); });
        return;
    }
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

    auto *logo = new QLabel(m_topBar);
    logo->setPixmap(grassBlockPixmap(5));
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

    auto *versionBadge = new QLabel(QStringLiteral("v0.5.0"), m_topBar);
    versionBadge->setObjectName(QStringLiteral("versionBadge"));
    layout->addWidget(versionBadge);

    auto *settingsBtn = new QPushButton(QStringLiteral("\u2699"), m_topBar);
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
    m_sidebar->setFixedWidth(300);

    auto *layout = new QVBoxLayout(m_sidebar);
    layout->setContentsMargins(18, 18, 0, 18);
    layout->setSpacing(14);

    // Карточка аккаунта
    auto *accountRow = new QWidget;
    auto *accountLayout = new QHBoxLayout(accountRow);
    accountLayout->setContentsMargins(0, 0, 0, 0);
    accountLayout->setSpacing(12);

    m_avatarLabel = new QLabel(accountRow);
    m_avatarLabel->setPixmap(steveHeadPixmap(6));
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

    // Карточка профилей
    m_addProfileButton = new QPushButton(QStringLiteral("＋  Добавить профиль"), m_sidebar);
    m_addProfileButton->setObjectName(QStringLiteral("addProfileButton"));
    m_addProfileButton->setCursor(Qt::PointingHandCursor);
    connect(m_addProfileButton, &QPushButton::clicked, this, &MainWindow::onAddProfileClicked);

    m_profileList = new QListWidget(m_sidebar);
    m_profileList->setObjectName(QStringLiteral("profileList"));
    m_profileList->setMinimumHeight(180);
    connect(m_profileList, &QListWidget::itemClicked, this,
            &MainWindow::onProfileListClicked);

    auto *profilesBox = new QWidget;
    auto *profilesLayout = new QVBoxLayout(profilesBox);
    profilesLayout->setContentsMargins(0, 0, 0, 0);
    profilesLayout->setSpacing(10);
    profilesLayout->addWidget(m_addProfileButton);
    profilesLayout->addWidget(m_profileList, 1);

    layout->addWidget(makeCard(QStringLiteral("ПРОФИЛИ"), profilesBox), 1);
}

// ---------- Панель запуска ----------

void MainWindow::buildLaunchPanel()
{
    m_launchPanel = new QFrame(m_central);
    m_launchPanel->setObjectName(QStringLiteral("launchPanel"));

    auto *layout = new QVBoxLayout(m_launchPanel);
    layout->setContentsMargins(48, 42, 48, 42);
    layout->setSpacing(16);
    layout->addStretch(1);

    auto *sectionTitle = new QLabel(QStringLiteral("ЗАПУСК ИГРЫ"), m_launchPanel);
    sectionTitle->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(sectionTitle, 0, Qt::AlignHCenter);

    auto *sectionSub = new QLabel(QStringLiteral("Выберите профиль слева или создайте новый"),
                                  m_launchPanel);
    sectionSub->setObjectName(QStringLiteral("sectionSub"));
    layout->addWidget(sectionSub, 0, Qt::AlignHCenter);

    // Данные выбранного профиля
    m_profileNameLabel = new QLabel(QStringLiteral("— профиль не выбран —"), m_launchPanel);
    m_profileNameLabel->setObjectName(QStringLiteral("profileName"));
    m_profileNameLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_profileNameLabel);

    m_profileDetailsLabel = new QLabel(QString(), m_launchPanel);
    m_profileDetailsLabel->setObjectName(QStringLiteral("profileDetails"));
    m_profileDetailsLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_profileDetailsLabel);

    m_profileStatusLabel = new QLabel(QString(), m_launchPanel);
    m_profileStatusLabel->setObjectName(QStringLiteral("profileStatus"));
    m_profileStatusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_profileStatusLabel);

    // Кнопка скачивания + прогресс
    m_downloadButton = new QPushButton(QStringLiteral("\u2b07  Скачать клиент"), m_launchPanel);
    m_downloadButton->setObjectName(QStringLiteral("downloadButton"));
    m_downloadButton->setFixedHeight(44);
    m_downloadButton->setCursor(Qt::PointingHandCursor);
    connect(m_downloadButton, &QPushButton::clicked, this, [this]() {
        for (const GameProfile &p : m_store->profiles()) {
            if (p.name == m_selectedProfile) {
                startDownload(p, false);
                break;
            }
        }
    });
    layout->addWidget(m_downloadButton);

    m_progressBar = new QProgressBar(m_launchPanel);
    m_progressBar->setObjectName(QStringLiteral("progressBar"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->hide();
    layout->addWidget(m_progressBar);

    // Кнопка запуска
    m_launchButton = new QPushButton(QStringLiteral("\u25b6   ЗАПУСТИТЬ"), m_launchPanel);
    m_launchButton->setObjectName(QStringLiteral("launchButton"));
    m_launchButton->setFixedHeight(56);
    m_launchButton->setCursor(Qt::PointingHandCursor);
    connect(m_launchButton, &QPushButton::clicked, this, &MainWindow::onLaunchClicked);
    layout->addWidget(m_launchButton);

    m_hintLabel = new QLabel(QStringLiteral("Загрузка списка версий…"), m_launchPanel);
    m_hintLabel->setObjectName(QStringLiteral("hint"));
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_hintLabel);

    layout->addStretch(1);

    updateMainPanel();
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

// ---------- Профили ----------

void MainWindow::refreshProfileList()
{
    m_profileList->clear();
    for (const GameProfile &p : m_store->profiles()) {
        auto *item = new QListWidgetItem;
        item->setText(QStringLiteral("%1\n%2 · %3")
                          .arg(p.name, p.versionId, loaderDisplayName(p.loader)));
        item->setIcon(QIcon(statusDot(statusColor(p))));
        item->setData(Qt::UserRole, p.name);
        item->setSizeHint(QSize(0, 48));
        m_profileList->addItem(item);
    }

    if (m_store->profiles().isEmpty())
        m_hintLabel->setText(
            QStringLiteral("Профилей пока нет. Нажмите «Добавить профиль», выберите "
                           "версию и загрузчик — или просто сохраните профиль, как игру в Steam."));
}

void MainWindow::selectProfile(const QString &name)
{
    m_selectedProfile = name;
    for (int i = 0; i < m_profileList->count(); ++i) {
        if (m_profileList->item(i)->data(Qt::UserRole).toString() == name) {
            m_profileList->setCurrentRow(i);
            break;
        }
    }
    updateMainPanel();
}

void MainWindow::onProfileListClicked()
{
    QListWidgetItem *item = m_profileList->currentItem();
    if (item)
        selectProfile(item->data(Qt::UserRole).toString());
}

void MainWindow::onAddProfileClicked()
{
    if (!m_versions) {
        showToast(QStringLiteral("Загрузка списка версий ещё не завершена — попробуйте через секунду"));
        return;
    }
    AddProfileDialog dialog(m_versions, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    GameProfile profile = dialog.resultProfile;
    // уникальное имя
    QString name = profile.name;
    int suffix = 2;
    const auto existing = [&](const QString &n) {
        for (const GameProfile &p : m_store->profiles())
            if (p.name == n)
                return true;
        return false;
    };
    while (existing(name))
        name = QStringLiteral("%1 (%2)").arg(profile.name).arg(suffix++);
    profile.name = name;

    m_store->addProfile(profile);
    refreshProfileList();
    selectProfile(profile.name);
    m_hintLabel->setText(QStringLiteral("Профиль «%1» добавлен").arg(profile.name));

    if (dialog.downloadNow())
        startDownload(profile, false);
}

void MainWindow::startDownload(const GameProfile &profile, bool launchAfterInstall)
{
    if (!m_installer || !m_versions) {
        showToast(QStringLiteral("Подготовка лаунчера…"));
        QTimer::singleShot(0, this, [this, profile, launchAfterInstall]() {
            startDownload(profile, launchAfterInstall);
        });
        return;
    }
    if (m_installer->isBusy()) {
        showToast(QStringLiteral("Уже идёт установка"));
        return;
    }

    m_launchAfterInstall = launchAfterInstall;

    // сбрасываем статус профиля
    GameProfile reset = profile;
    reset.downloaded = false;
    reset.downloadError.clear();
    m_store->updateProfile(reset);

    m_profileStatusLabel->setText(QStringLiteral("подготовка…"));
    m_progressBar->setValue(0);
    m_progressBar->show();
    m_downloadButton->setEnabled(false);
    m_launchButton->setEnabled(false);

    m_installer->install(profile.versionId, profile.loader, m_store->dataDir(),
                         gameDirFor(profile),
                         [this](const QString &id) { return m_versions->versionJsonUrl(id); });
    refreshProfileList();
}

QString MainWindow::gameDirFor(const GameProfile &profile) const
{
    return QDir(m_store->dataDir()).filePath(
        QStringLiteral("games/") + ProfileStore::sanitizeName(profile.name));
}

// ---------- Список версий ----------

void MainWindow::onVersionsLoaded()
{
    const int count = m_versions->versions().size();
    m_statusRight->setText(QStringLiteral("профилей: %1 · версий: %2")
                               .arg(m_store->profiles().size())
                               .arg(count));
    if (m_manualRefresh)
        showToast(QStringLiteral("Список версий обновлён (%1 шт.)").arg(count));
    m_manualRefresh = false;
}

void MainWindow::onVersionsFailed()
{
    m_statusRight->setText(QStringLiteral("список версий недоступен"));
    if (m_manualRefresh)
        showToast(QStringLiteral("Не удалось обновить список версий"));
    m_manualRefresh = false;
}

// ---------- Панель запуска: состояние ----------

void MainWindow::updateMainPanel()
{
    // Вызывается из buildLaunchPanel до создания бэкенда — тогда кнопки
    // блокируем и не трогаем установщик.
    if (!m_store)
        return;
    if (!m_installer) {
        if (m_downloadButton) m_downloadButton->setEnabled(false);
        if (m_launchButton) m_launchButton->setEnabled(false);
        if (m_hintLabel)
            m_hintLabel->setText(QStringLiteral("Подготовка лаунчера…"));
        return;
    }

    const bool hasProfile = !m_selectedProfile.isEmpty();
    GameProfile selected;
    for (const GameProfile &p : m_store->profiles()) {
        if (p.name == m_selectedProfile) {
            selected = p;
            break;
        }
    }

    if (!hasProfile) {
        m_profileNameLabel->setText(QStringLiteral("— профиль не выбран —"));
        m_profileDetailsLabel->clear();
        m_profileStatusLabel->clear();
        m_downloadButton->setEnabled(false);
        m_launchButton->setEnabled(false);
        return;
    }

    m_profileNameLabel->setText(selected.name);
    m_profileDetailsLabel->setText(
        QStringLiteral("Версия %1 · %2")
            .arg(selected.versionId, loaderDisplayName(selected.loader)));

    const bool busy = m_installer->isBusy();
    const bool running = m_installer->isGameRunning();

    if (busy) {
        // статус уже показывает прогресс
    } else {
        m_profileStatusLabel->setText(statusLabel(selected));
        QColor color = statusColor(selected);
        m_profileStatusLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(color.name()));
    }

    m_downloadButton->setEnabled(!selected.downloaded && !busy && !running);
    m_downloadButton->setText(selected.downloaded
                                  ? QStringLiteral("\u2713  Игра установлена")
                                  : QStringLiteral("\u2b07  Скачать игру"));

    m_launchButton->setEnabled(!busy);
    m_launchButton->setText(running
                                ? QStringLiteral("\u23f9  Остановить игру")
                                : QStringLiteral("\u25b6   ЗАПУСТИТЬ"));
}

// ---------- Действия ----------

void MainWindow::onLaunchClicked()
{
    if (!m_installer) {
        showToast(QStringLiteral("Подготовка лаунчера…"));
        return;
    }
    // Игра запущена — кнопка работает как «Остановить»
    if (m_installer->isGameRunning()) {
        m_installer->stopGame();
        showToast(QStringLiteral("Останавливаю игру…"));
        return;
    }

    if (m_installer->isBusy()) {
        showToast(QStringLiteral("Сейчас идёт установка — подождите"));
        return;
    }

    if (m_selectedProfile.isEmpty()) {
        showToast(QStringLiteral("Сначала создайте профиль"));
        return;
    }

    GameProfile selected;
    for (const GameProfile &p : m_store->profiles()) {
        if (p.name == m_selectedProfile) {
            selected = p;
            break;
        }
    }

    if (selected.loader == QLatin1String("forge") || selected.loader == QLatin1String("neoforge")) {
        showToast(QStringLiteral("Запуск %1 пока не поддерживается — выберите Vanilla, Fabric или Quilt")
                      .arg(loaderDisplayName(selected.loader)));
        return;
    }

    if (!selected.downloaded) {
        // Полная установка и сразу запуск
        showToast(QStringLiteral("Скачиваю игру «%1» — запущу, как будет готово…").arg(selected.name));
        startDownload(selected, true);
        return;
    }

    launchProfile(selected);
}

void MainWindow::launchProfile(const GameProfile &profile)
{
    if (!m_installer) {
        showToast(QStringLiteral("Подготовка лаунчера…"));
        return;
    }
    if (m_installer->isGameRunning())
        return;

    const QString dataDir = m_store->dataDir();
    const QString gameDir = gameDirFor(profile);

    // Подготавливаем нужную для версии Java (находит подходящую или скачивает)
    // и сразу запускаем.
    m_profileStatusLabel->setText(QStringLiteral("Java…"));
    m_progressBar->setValue(0);
    m_progressBar->show();
    m_installer->ensureJava(profile.versionId, dataDir,
                            [this, profile, dataDir, gameDir](bool ok, const QString &err) {
        m_progressBar->hide();
        if (!ok) {
            showToast(QStringLiteral("Не удалось получить Java: %1").arg(err));
            return;
        }
        if (!m_installer->launchGame(profile.versionId, dataDir, gameDir))
            showToast(QStringLiteral("Не удалось запустить игру: не все файлы на месте. Нажмите «Скачать игру»"));
    });
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

