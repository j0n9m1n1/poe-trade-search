// SPDX-License-Identifier: GPL-3.0-or-later
//
// 디버그용 CLI: 아이템 텍스트 파일 -> 파싱 결과 / 쿼리 JSON / (옵션) 실제 검색, 시세
//   pts_cli --data <dir> [--game poe1|poe2|auto] [--league L] [--server ko|en] [--search] [--price N]
//           [--update] item.txt
#include "core/CheckedStore.h"
#include "core/ItemParser.h"
#include "core/Keywords.h"
#include "core/PriceSummary.h"
#include "core/SearchDefaults.h"
#include "net/DataManager.h"
#include "net/Endpoints.h"
#include "net/TradeClient.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>

using namespace pts;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_INIT_RESOURCE(resources);
    QTextStream out(stdout);

    QCommandLineParser cl;
    cl.addHelpOption();
    cl.addOption({QStringLiteral("data"), QStringLiteral("data dir (cache/ 가 들어있는 폴더)"), QStringLiteral("dir")});
    cl.addOption({QStringLiteral("game"), QStringLiteral("poe1 | poe2 | auto"), QStringLiteral("game"), QStringLiteral("auto")});
    cl.addOption({QStringLiteral("league"), QStringLiteral("league"), QStringLiteral("league"), QStringLiteral("Standard")});
    cl.addOption({QStringLiteral("server"), QStringLiteral("ko | en (기본: 아이템 언어)"), QStringLiteral("server")});
    cl.addOption({QStringLiteral("search"), QStringLiteral("실제로 검색해서 결과 수와 URL 출력")});
    cl.addOption({QStringLiteral("price"), QStringLiteral("시세 N개 조회"), QStringLiteral("n")});
    cl.addOption({QStringLiteral("update"), QStringLiteral("거래소 데이터 먼저 받기")});
    cl.addPositionalArgument(QStringLiteral("item"), QStringLiteral("아이템 텍스트 파일"));
    cl.process(app);

    const QString dataDir = cl.value(QStringLiteral("data"));
    if (dataDir.isEmpty() || cl.positionalArguments().isEmpty()) {
        cl.showHelp(1);
    }

    Keywords kw;
    QString err;
    if (!kw.load(QStringLiteral(":/data/parser.json"), &err)) {
        out << "parser.json: " << err << Qt::endl;
        return 1;
    }
    QFile f(cl.positionalArguments().first());
    if (!f.open(QIODevice::ReadOnly)) {
        out << "cannot open item file" << Qt::endl;
        return 1;
    }
    const QString text = QString::fromUtf8(f.readAll());

    TradeClient client;
    DataManager dm(dataDir, &client);
    GameDb dbs[2];
    CheckedStore checked;
    ItemParser parser(kw, dbs, &checked);

    Game game = Game::Poe1;
    const QString gs = cl.value(QStringLiteral("game"));
    if (gs == QStringLiteral("poe2"))
        game = Game::Poe2;
    else if (gs == QStringLiteral("auto"))
        game = parser.guessGame(text, Game::Poe1);

    auto run = [&]() {
        if (!dm.load(game, &dbs[gi(game)], kw, &err)) {
            out << "data load failed: " << err << Qt::endl;
            app.exit(1);
            return;
        }
        auto it = parser.parse(text, game, ParseOptions{}, &err);
        if (!it) {
            out << "parse failed: " << err << Qt::endl;
            app.exit(1);
            return;
        }
        out << "game=" << gameKey(it->game) << " lang=" << langKey(it->lang) << " class=" << it->itemClass
            << " category=" << it->category << " rarity=" << it->rarityId << Qt::endl;
        out << "name=" << it->name << " type=" << it->type << (it->disc.isEmpty() ? QString() : QStringLiteral(" disc=") + it->disc)
            << " ilvl=" << it->itemLevel << " q=" << it->quality << " sockets=" << it->sockets << " links=" << it->links
            << Qt::endl;
        if (it->hasDps)
            out << QStringLiteral("DPS p=%1 e=%2 c=%3").arg(it->pdps, 0, 'f', 1).arg(it->edps, 0, 'f', 1).arg(it->cdps, 0, 'f', 1)
                << Qt::endl;
        if (!it->exchangeId.isEmpty())
            out << "exchange id=" << it->exchangeId << Qt::endl;
        for (const auto& m : it->mods) {
            const ModCandidate* c = m.current();
            out << (m.checked ? "[x] " : "[ ] ") << m.text << "  <" << (c ? c->id : QString())
                << (c && !c->option.isUndefined() && !c->option.isNull() ? QStringLiteral("|opt=") + QString::number(c->option.toInt()) : QString())
                << "> min=" << (m.min ? QString::number(*m.min) : QStringLiteral("-"))
                << " max=" << (m.max ? QString::number(*m.max) : QStringLiteral("-"))
                << (m.modType.isEmpty() ? QString() : QStringLiteral(" type=") + m.modType)
                << (m.totalRes ? QStringLiteral(" totalres") : QString()) << Qt::endl;
        }
        for (const QString& u : it->unmatched)
            out << "  (unmatched) " << u << Qt::endl;

        RequestContext ctx;
        ctx.server = it->lang;
        if (cl.value(QStringLiteral("server")) == QStringLiteral("ko"))
            ctx.server = Lang::Ko;
        else if (cl.value(QStringLiteral("server")) == QStringLiteral("en"))
            ctx.server = Lang::En;
        ctx.forBrowser = false;
        QString warn;
        const SearchRequest req = makeRequest(*it, defaultFilterState(*it), kw, &dbs[gi(game)], ctx, &warn);
        if (!warn.isEmpty())
            out << "warning: " << warn << Qt::endl;
        const QJsonObject q = QueryBuilder::build(req, &dbs[gi(game)][req.lang]);
        out << QJsonDocument(q).toJson(QJsonDocument::Indented);

        const QString league = cl.value(QStringLiteral("league"));
        if (cl.isSet(QStringLiteral("price"))) {
            const int n = cl.value(QStringLiteral("price")).toInt();
            client.priceCheck(
                game, req.lang, league, q, n, nullptr,
                [&](const TradeClient::PriceProgress& all, const QString& e) {
                    if (!e.isEmpty())
                        out << "price error: " << e << Qt::endl;
                    out << "total=" << all.total << " fetched=" << all.listings.size() << Qt::endl;
                    for (const auto& l : all.listings)
                        out << "  " << PriceSummary::formatAmount(l.amount) << " " << l.currency << " [" << l.account << "] "
                            << PriceSummary::ago(l.indexed) << Qt::endl;
                    out << PriceSummary::summarize(all.listings, nullptr) << Qt::endl;
                    app.exit(e.isEmpty() ? 0 : 2);
                },
                client.newToken());
        } else if (cl.isSet(QStringLiteral("search"))) {
            client.search(game, req.lang, league, q, [&out, &app, league, g = game, l = req.lang](const TradeClient::SearchResult& r) {
                if (!r.error.isEmpty()) {
                    out << "search error: " << r.error << Qt::endl;
                    app.exit(2);
                    return;
                }
                out << "total=" << r.total << " url=" << endpoints::webSearch(g, l, league, r.id).toString() << Qt::endl;
                app.exit(0);
            });
        } else {
            app.exit(0);
        }
    };

    if (cl.isSet(QStringLiteral("update"))) {
        QObject::connect(&dm, &DataManager::progress, [&](const QString& m) { out << m << Qt::endl; });
        dm.update(game, [&](bool ok, const QString& e) {
            if (!ok)
                out << "update failed: " << e << Qt::endl;
            run();
        });
    } else {
        QTimer::singleShot(0, run);
    }
    return app.exec();
}
