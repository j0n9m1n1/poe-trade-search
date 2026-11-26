// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Types.h"

#include <QString>
#include <QStringList>
#include <QUrl>

namespace pts {

// 서버 주소 (2026-11 실측)
//  - 한국(카카오): https://poe.kakaogames.com  (구 poe.game.daum.net 은 DNS 없음)
//  - 글로벌     : https://www.pathofexile.com
//  - PoE2 는 /api/trade2, /trade2 + realm "poe2"
namespace endpoints {

QString base(Lang server);
QUrl data(Game g, Lang server, const QString& what); // leagues / stats / items / static / filters
QUrl search(Game g, Lang server, const QString& league);
QUrl fetch(Game g, Lang server, const QStringList& ids, const QString& queryId);
QUrl exchange(Game g, Lang server, const QString& league);
QUrl webSearch(Game g, Lang server, const QString& league, const QString& id);
QUrl webExchange(Game g, Lang server, const QString& league, const QString& id);
QUrl redirectSearch(Game g, Lang server, const QString& league, const QByteArray& queryJson);

} // namespace endpoints
} // namespace pts
