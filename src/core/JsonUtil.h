// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QString>

namespace pts::json {

// BOM, // 주석, 끝 콤마를 허용하는 관대한 파서 (원본 Config.txt / Checked.txt 호환용)
QJsonDocument parseLenient(const QByteArray& data, QString* error = nullptr);

bool readFile(const QString& path, QJsonDocument* out, QString* error = nullptr);

// QSaveFile로 원자적 저장. 원본처럼 UTF-8 BOM 붙여서 저장 가능
bool writeFile(const QString& path, const QJsonDocument& doc, bool indented, bool bom, QString* error = nullptr);

bool writeRaw(const QString& path, const QByteArray& data, QString* error = nullptr);

} // namespace pts::json
