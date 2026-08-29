#include "versionmodel.h"

#include "VersionManager.h"

#include <QBrush>
#include <QColor>
#include <QStandardItem>
#include <QStandardItemModel>

namespace {

QStandardItem *makeSectionItem(const QString &text)
{
    auto *item = new QStandardItem(text);
    item->setFlags(Qt::NoItemFlags); // не выбирается
    item->setSelectable(false);
    item->setForeground(QBrush(QColor(0x8b, 0x98, 0xa8)));
    return item;
}

} // namespace

QStandardItemModel *buildVersionModel(QObject *parent,
                                      const QVector<VersionInfo> &versions,
                                      const QString &latestRelease,
                                      const QString &latestSnapshot)
{
    auto *model = new QStandardItemModel(parent);
    if (versions.isEmpty())
        return model;

    // --- "Последние версии" ---
    model->appendRow(makeSectionItem(QStringLiteral("ПОСЛЕДНИЕ ВЕРСИИ")));
    const auto addLatest = [&](const QString &id, const QString &suffix) {
        if (id.isEmpty())
            return;
        auto *item = new QStandardItem(QStringLiteral("%1  %2").arg(id, suffix));
        item->setData(id, Qt::UserRole);
        item->setForeground(QBrush(QColor(0x3d, 0xdc, 0x68)));
        model->appendRow(item);
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
        model->appendRow(makeSectionItem(title));
        for (QStandardItem *item : items)
            model->appendRow(item);
    };

    appendGroup(QStringLiteral("РЕЛИЗЫ"), { QStringLiteral("release") },
                QColor(0xe8, 0xee, 0xf5));
    appendGroup(QStringLiteral("СНАПШОТЫ"), { QStringLiteral("snapshot") },
                QColor(0x9a, 0xa6, 0xb5));
    appendGroup(QStringLiteral("СТАРЫЕ БЕТЫ И АЛЬФЫ"),
                { QStringLiteral("old_beta"), QStringLiteral("old_alpha") },
                QColor(0x7a, 0x86, 0x96));

    return model;
}
