// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/JsonUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QSaveFile>

namespace pts::json {

namespace {

QByteArray stripBom(const QByteArray& d)
{
    if (d.startsWith("\xEF\xBB\xBF"))
        return d.mid(3);
    return d;
}

// 문자열 밖의 // 주석과 끝 콤마 제거
QByteArray sanitize(const QByteArray& in)
{
    QByteArray out;
    out.reserve(in.size());
    bool inStr = false;
    bool esc = false;
    for (int i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (inStr) {
            out += c;
            if (esc)
                esc = false;
            else if (c == '\\')
                esc = true;
            else if (c == '"')
                inStr = false;
            continue;
        }
        if (c == '"') {
            inStr = true;
            out += c;
            continue;
        }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n')
                ++i;
            out += '\n';
            continue;
        }
        if (c == ',') {
            int j = i + 1;
            while (j < in.size() && (in[j] == ' ' || in[j] == '\t' || in[j] == '\r' || in[j] == '\n'))
                ++j;
            if (j < in.size() && (in[j] == ']' || in[j] == '}'))
                continue; // 끝 콤마 버림
        }
        out += c;
    }
    return out;
}

} // namespace

QJsonDocument parseLenient(const QByteArray& data, QString* error)
{
    const QByteArray d = stripBom(data);
    QJsonParseError pe{};
    QJsonDocument doc = QJsonDocument::fromJson(d, &pe);
    if (pe.error == QJsonParseError::NoError)
        return doc;
    doc = QJsonDocument::fromJson(sanitize(d), &pe);
    if (pe.error != QJsonParseError::NoError && error)
        *error = pe.errorString() + QStringLiteral(" @") + QString::number(pe.offset);
    return doc;
}

bool readFile(const QString& path, QJsonDocument* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QString err;
    QJsonDocument doc = parseLenient(f.readAll(), &err);
    if (doc.isNull()) {
        if (error)
            *error = err;
        return false;
    }
    if (out)
        *out = doc;
    return true;
}

bool writeRaw(const QString& path, const QByteArray& data, QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool writeFile(const QString& path, const QJsonDocument& doc, bool indented, bool bom, QString* error)
{
    QByteArray data = doc.toJson(indented ? QJsonDocument::Indented : QJsonDocument::Compact);
    if (bom)
        data.prepend("\xEF\xBB\xBF");
    return writeRaw(path, data, error);
}

} // namespace pts::json
