#pragma once

#include <QByteArray>
#include <QString>

// Диагностика пустого ответа или HTML-заглушки (антивирус / фильтр / прокси),
// которые часто подменяют файлы с mojang.com.

inline bool payloadLooksLikeHtml(const QByteArray &data)
{
    const QByteArray t = data.trimmed().toLower();
    if (t.isEmpty())
        return false;
    return t.startsWith("<!doctype") || t.startsWith("<html")
        || t.startsWith("<head") || t.startsWith("<body")
        || t.contains("<html");
}

inline QString describeBadPayload(const QByteArray &data)
{
    const QString preview = QString::fromUtf8(data.left(80)).simplified();
    return QStringLiteral("%1 байт, первые символы: «%2». Если там HTML — доступ к mojang.com блокируется (антивирус/пр)")
        .arg(data.size())
        .arg(preview);
}

inline bool isBadDownloadPayload(const QByteArray &data)
{
    return data.isEmpty() || payloadLooksLikeHtml(data);
}
