#pragma once

#include "ProfileStore.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class VersionManager;

// Диалог создания профиля: имя, версия, загрузчик.
// Две кнопки действия, как у игр в Steam:
//   «Скачать и добавить» — профиль создаётся и сразу скачивается клиент
//   «Добавить без скачивания» — профиль просто сохраняется
class AddProfileDialog : public QDialog
{
public:
    explicit AddProfileDialog(VersionManager *versions, QWidget *parent = nullptr);

    GameProfile resultProfile;  // заполняется после exec() == Accepted
    bool downloadNow() const { return m_downloadNow; }

private:
    void populateVersions();
    void onAccepted(bool download);

    VersionManager *m_versions = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_versionCombo = nullptr;
    QComboBox *m_loaderCombo = nullptr;
    bool m_downloadNow = false;
};
