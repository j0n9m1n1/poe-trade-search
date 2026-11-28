// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/Endpoints.h"

#include <QHash>
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
    // QUrlQuery 는 '+' 와 '%XX' 를 그대로 둬서 서버가 공백/다른 글자로 읽음 -> 값 전체를 직접 인코딩
    u.setQuery(QStringLiteral("q=") + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(queryJson))),
               QUrl::StrictMode);
    return u;
}

QString itemSymbol(const QString& name)
{
    // pathofexile.com CSS(.itemHeader .symbol.*) 에서 가져온 주소. key 가 바뀌면 받기가 실패하고 오브 아이콘으로 넘어감
    static const QHash<QString, QString> urls = {
        {QStringLiteral("fractured"), QStringLiteral("fractured-symbol.png?v=1709582916256&key=fM_bl3SW2hozdEIw5vh4GA")},
        {QStringLiteral("shaper"), QStringLiteral("shaper-symbol.png?v=1709582916256&key=PQH0ATL4n268OngTnsQ0MQ")},
        {QStringLiteral("elder"), QStringLiteral("elder-symbol.png?v=1709582916256&key=YS6Fsnxyuc8gpqfPz_mykw")},
        {QStringLiteral("crusader"), QStringLiteral("crusader-symbol.png?v=1709582916256&key=dcLGV7cDJf-dulB8P_Q-yQ")},
        {QStringLiteral("redeemer"), QStringLiteral("redeemer-symbol.png?v=1709582916256&key=nEc-TPctqwszY696cmlaZQ")},
        {QStringLiteral("hunter"), QStringLiteral("hunter-symbol.png?v=1709582916256&key=x7-F-t4jlxzMd1eJOUSafA")},
        {QStringLiteral("warlord"), QStringLiteral("warlord-symbol.png?v=1709582916256&key=76scs3jk3FrVGOLHdM5AuA")},
        {QStringLiteral("searing"), QStringLiteral("searing-symbol.png?v=1709582916256&key=iiOWJvWHj8-0bH0e28X7Xw")},
        {QStringLiteral("tangled"), QStringLiteral("tangled-symbol.png?v=1709582916256&key=b-SVlhPy2jy-UJSTBaV2Cw")},
    };
    const QString u = urls.value(name);
    return u.isEmpty() ? QString() : QStringLiteral("https://web.poecdn.com/protected/image/item/popup/") + u;
}

} // namespace pts::endpoints
