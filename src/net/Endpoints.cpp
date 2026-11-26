// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/Endpoints.h"

#include <QUrlQuery>

namespace pts::endpoints {

namespace {
QString enc(const QString& s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}
QString api(Game g, Lang server)
{
    return base(server) + (g == Game::Poe1 ? QStringLiteral("/api/trade") : QStringLiteral("/api/trade2"));
}
QString realmPath(Game g)
{
    return g == Game::Poe1 ? QString() : QStringLiteral("poe2/");
}
QString web(Game g, Lang server)
{
    return base(server) + (g == Game::Poe1 ? QStringLiteral("/trade") : QStringLiteral("/trade2"));
}
} // namespace

QString base(Lang server)
{
    return server == Lang::Ko ? QStringLiteral("https://poe.kakaogames.com") : QStringLiteral("https://www.pathofexile.com");
}

QUrl data(Game g, Lang server, const QString& what)
{
    return QUrl(api(g, server) + QStringLiteral("/data/") + what);
}

QUrl search(Game g, Lang server, const QString& league)
{
    return QUrl(api(g, server) + QStringLiteral("/search/") + realmPath(g) + enc(league));
}

QUrl fetch(Game g, Lang server, const QStringList& ids, const QString& queryId)
{
    QUrl u(api(g, server) + QStringLiteral("/fetch/") + ids.join(QLatin1Char(',')));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("query"), queryId);
    if (g == Game::Poe2)
        q.addQueryItem(QStringLiteral("realm"), QStringLiteral("poe2"));
    u.setQuery(q);
    return u;
}

QUrl exchange(Game g, Lang server, const QString& league)
{
    return QUrl(api(g, server) + QStringLiteral("/exchange/") + realmPath(g) + enc(league));
}

QUrl webSearch(Game g, Lang server, const QString& league, const QString& id)
{
    return QUrl(web(g, server) + QStringLiteral("/search/") + realmPath(g) + enc(league) + QLatin1Char('/') + id);
}

QUrl webExchange(Game g, Lang server, const QString& league, const QString& id)
{
    return QUrl(web(g, server) + QStringLiteral("/exchange/") + realmPath(g) + enc(league) + QLatin1Char('/') + id);
}

QUrl redirectSearch(Game g, Lang server, const QString& league, const QByteArray& queryJson)
{
    // 원본 server_redirect: 브라우저가 직접 검색 POST 를 대신하도록
    QUrl u(web(g, server) + QStringLiteral("/search/") + realmPath(g) + enc(league));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), QString::fromUtf8(queryJson));
    u.setQuery(q);
    return u;
}

QUrl wiki(Game g, const QString& title)
{
    QString t = title;
    t.replace(QLatin1Char(' '), QLatin1Char('_'));
    const QString host = g == Game::Poe1 ? QStringLiteral("https://www.poewiki.net/wiki/")
                                         : QStringLiteral("https://www.poe2wiki.net/wiki/");
    return QUrl(host + enc(t));
}

} // namespace pts::endpoints
