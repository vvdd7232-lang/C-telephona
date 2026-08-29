#pragma once

#include <QString>
#include <QVector>

class QObject;
class QStandardItemModel;
struct VersionInfo;

// Группированный список версий Minecraft для QComboBox:
//   ПОСЛЕДНИЕ ВЕРСИИ (релиз ★ / снапшот ☆), РЕЛИЗЫ, СНАПШОТЫ, СТАРЫЕ БЕТЫ И АЛЬФЫ
// В UserRole каждого элемента — id версии.
QStandardItemModel *buildVersionModel(QObject *parent,
                                      const QVector<VersionInfo> &versions,
                                      const QString &latestRelease,
                                      const QString &latestSnapshot);
