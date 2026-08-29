#include "AddProfileDialog.h"

#include "VersionManager.h"
#include "versionmodel.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

AddProfileDialog::AddProfileDialog(VersionManager *versions, QWidget *parent)
    : QDialog(parent)
    , m_versions(versions)
{
    setWindowTitle(QStringLiteral("Добавить профиль"));
    setModal(true);
    setMinimumWidth(430);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("НОВЫЙ ПРОФИЛЬ"), this);
    title->setObjectName(QStringLiteral("dialogTitle"));
    layout->addWidget(title);

    auto *sub = new QLabel(
        QStringLiteral("Профиль — это связка «версия + загрузчик». Можно добавить его "
                       "сразу со скачиванием клиента или просто сохранить, как игру в Steam."),
        this);
    sub->setObjectName(QStringLiteral("dialogSub"));
    sub->setWordWrap(true);
    layout->addWidget(sub);

    // Имя
    auto *nameLabel = new QLabel(QStringLiteral("НАЗВАНИЕ ПРОФИЛЯ"), this);
    nameLabel->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(nameLabel);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    m_nameEdit->setPlaceholderText(QStringLiteral("Например: Выживание"));
    m_nameEdit->setFixedHeight(38);
    layout->addWidget(m_nameEdit);

    // Версия
    auto *versionLabel = new QLabel(QStringLiteral("ВЕРСИЯ"), this);
    versionLabel->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(versionLabel);

    m_versionCombo = new QComboBox(this);
    m_versionCombo->setObjectName(QStringLiteral("versionCombo"));
    m_versionCombo->setFixedHeight(38);
    populateVersions();
    layout->addWidget(m_versionCombo);

    // Загрузчик
    auto *loaderLabel = new QLabel(QStringLiteral("ЗАГРУЗЧИК"), this);
    loaderLabel->setObjectName(QStringLiteral("fieldLabel"));
    layout->addWidget(loaderLabel);

    m_loaderCombo = new QComboBox(this);
    m_loaderCombo->setObjectName(QStringLiteral("loaderCombo"));
    m_loaderCombo->setFixedHeight(38);
    for (const QString &id : loaderIds()) {
        m_loaderCombo->addItem(loaderDisplayName(id), id);
    }
    layout->addWidget(m_loaderCombo);

    auto *loaderNote = new QLabel(
        QStringLiteral("Версия загрузчика — последняя доступная (автоматически)"), this);
    loaderNote->setObjectName(QStringLiteral("dialogNote"));
    layout->addWidget(loaderNote);

    layout->addStretch(1);

    // Кнопки
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);

    auto *downloadBtn = new QPushButton(QStringLiteral("\u2b07  Скачать и добавить"), this);
    downloadBtn->setObjectName(QStringLiteral("downloadButtonDialog"));
    downloadBtn->setCursor(Qt::PointingHandCursor);
    connect(downloadBtn, &QPushButton::clicked, this, [this]() { onAccepted(true); });
    buttons->addWidget(downloadBtn, 1);

    auto *saveBtn = new QPushButton(QStringLiteral("Добавить без скачивания"), this);
    saveBtn->setObjectName(QStringLiteral("saveButton"));
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, [this]() { onAccepted(false); });
    buttons->addWidget(saveBtn, 1);

    layout->addLayout(buttons);

    auto *cancelBtn = new QPushButton(QStringLiteral("Отмена"), this);
    cancelBtn->setObjectName(QStringLiteral("cancelButton"));
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(cancelBtn, 0, Qt::AlignHCenter);
}

void AddProfileDialog::populateVersions()
{
    const auto versions = m_versions->versions();
    m_versionCombo->setModel(buildVersionModel(this, versions, m_versions->latestRelease(),
                                               m_versions->latestSnapshot()));
    if (versions.isEmpty()) {
        m_versionCombo->addItem(QStringLiteral("— версии не загружены —"));
    } else {
        // выбираем последний релиз
        const QString latest = m_versions->latestRelease();
        const QStandardItemModel *model =
            qobject_cast<QStandardItemModel *>(m_versionCombo->model());
        for (int i = 0; model && i < model->rowCount(); ++i) {
            if (model->item(i)->data(Qt::UserRole).toString() == latest) {
                m_versionCombo->setCurrentIndex(i);
                break;
            }
        }
    }
}

void AddProfileDialog::onAccepted(bool download)
{
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        name = QStringLiteral("Профиль");

    const QString versionId = m_versionCombo->currentData().toString();
    if (versionId.isEmpty()) {
        return; // версии не загружены — не даём создать
    }

    resultProfile.name = name;
    resultProfile.versionId = versionId;
    resultProfile.loader = m_loaderCombo->currentData().toString();
    resultProfile.downloaded = false;
    resultProfile.createdAt = QDateTime::currentDateTimeUtc()
                                  .toString(Qt::ISODate);
    m_downloadNow = download;
    accept();
}
