// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/TradeClient.h"

#include "core/TextUtil.h"
#include "net/Endpoints.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <memory>

namespace pts {

namespace {
qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString apiError(const QByteArray& body)
{
    const QJsonObject e = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toObject();
    return e.value(QStringLiteral("message")).toString();
}

QDateTime parseTime(const QString& s)
{
    QDateTime t = QDateTime::fromString(s, Qt::ISODate);
    if (!t.isValid())
        t = QDateTime::fromString(s, Qt::ISODateWithMs);
    return t;
}

QStringList toStringList(const QJsonValue& v)
{
    QStringList out;
    for (const auto& e : v.toArray()) {
        const QString s = e.toString();
        if (!s.isEmpty())
            out << s;
    }
    return out;
}
} // namespace

TradeClient::TradeClient(QObject* parent) : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    m_userAgent = QStringLiteral("PoeTradeSearchQt/%1").arg(QStringLiteral(PTS_VERSION));
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &TradeClient::pump);
}

qint64 TradeClient::waitMs(Policy p) const
{
    return m_limiter.waitMs(p, nowMs());
}

void TradeClient::get(Policy p, const QUrl& url, RawCallback cb, quint64 token)
{
    m_queue.enqueue({p, url, {}, false, std::move(cb), token});
    pump();
}

void TradeClient::post(Policy p, const QUrl& url, const QByteArray& json, RawCallback cb, quint64 token)
{
    m_queue.enqueue({p, url, json, true, std::move(cb), token});
    pump();
}

void TradeClient::cancel(quint64 token)
{
    if (token == 0)
        return;
    QQueue<Job> keep;
    while (!m_queue.isEmpty()) {
        Job j = m_queue.dequeue();
        if (j.token != token)
            keep.enqueue(j);
    }
    m_queue = keep;
    if (m_running && m_runningJob.token == token) {
        m_runningJob.cb = nullptr; // 콜백 버림
        m_running->abort();
    } else if (!m_running) {
        // 대기 중이던 작업이 취소됐으면 타이머를 재계산해 다음 작업을 이어서 처리
        m_timer.stop();
        pump();
    }
}

void TradeClient::pump()
{
    if (m_running || m_queue.isEmpty())
        return;
    const qint64 now = nowMs();
    int readyIdx = -1;
    qint64 minWait = -1;
    for (int i = 0; i < m_queue.size(); ++i) {
        const qint64 w = m_limiter.waitMs(m_queue[i].policy, now);
        if (w <= 0) {
            readyIdx = i;
            break;
        }
        if (minWait < 0 || w < minWait)
            minWait = w;
    }
    if (readyIdx >= 0) {
        start(m_queue.takeAt(readyIdx));
        return;
    }
    if (minWait > 1500)
        emit throttled(static_cast<int>((minWait + 999) / 1000));
    m_timer.start(static_cast<int>(qMin<qint64>(minWait, 60 * 60 * 1000)));
}

void TradeClient::start(const Job& j)
{
    QNetworkRequest req(j.url);
    req.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(m_timeoutSec * 1000);
    if (j.isPost)
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    m_limiter.record(j.policy, nowMs());
    m_runningJob = j;
    m_running = j.isPost ? m_nam->post(req, j.body) : m_nam->get(req);
    QNetworkReply* reply = m_running;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const Job job = m_runningJob;
        m_running = nullptr;
        reply->deleteLater();

        m_limiter.update(job.policy, [reply](const QByteArray& h) { return reply->rawHeader(h); }, nowMs());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429) {
            const int retry = reply->rawHeader("Retry-After").toInt();
            m_limiter.block(job.policy, nowMs() + static_cast<qint64>(qMax(retry, 10)) * 1000);
        }
        const QByteArray body = reply->readAll();
        QString err;
        if (reply->error() != QNetworkReply::NoError) {
            err = apiError(body);
            if (err.isEmpty())
                err = status == 429 ? QStringLiteral("요청이 너무 많습니다 (요청 제한)") : reply->errorString();
        }
        if (job.cb)
            job.cb(status, body, err);
        pump();
    });
}

void TradeClient::search(Game g, Lang server, const QString& league, const QJsonObject& query,
                         std::function<void(const SearchResult&)> cb, quint64 token)
{
    post(Policy::Search, endpoints::search(g, server, league), QJsonDocument(query).toJson(QJsonDocument::Compact),
         [cb](int, const QByteArray& body, const QString& error) {
             SearchResult r;
             r.error = error;
             if (error.isEmpty()) {
                 const QJsonObject o = QJsonDocument::fromJson(body).object();
                 r.id = o.value(QStringLiteral("id")).toString();
                 r.total = o.value(QStringLiteral("total")).toInt();
                 for (const auto& v : o.value(QStringLiteral("result")).toArray())
                     r.ids << v.toString();
                 if (r.id.isEmpty())
                     r.error = QStringLiteral("검색 응답 형식이 올바르지 않습니다");
             }
             cb(r);
         },
         token);
}

void TradeClient::priceCheck(Game g, Lang server, const QString& league, const QJsonObject& query, int count,
                             std::function<void(const PriceProgress&)> progress,
                             std::function<void(const PriceProgress&, const QString&)> done, quint64 token)
{
    search(g, server, league, query, [this, g, server, count, progress, done, token](const SearchResult& sr) {
        auto all = std::make_shared<PriceProgress>();
        if (!sr.error.isEmpty()) {
            done(*all, sr.error);
            return;
        }
        all->search = sr;
        all->total = sr.total;
        all->requested = qBound(0, count, static_cast<int>(sr.ids.size()));
        fetchFirstPricePage(g, server, all, progress, done, token);
    }, token);
}

void TradeClient::fetchFirstPricePage(Game g, Lang server, const std::shared_ptr<PriceProgress>& all,
                                      std::function<void(const PriceProgress&)> progress,
                                      std::function<void(const PriceProgress&, const QString&)> done, quint64 token)
{
    if (all->fetched == all->requested) {
        done(*all, QString());
        return;
    }
    const QStringList chunk = all->search.ids.mid(all->fetched, qMin(10, all->requested - all->fetched));
    fetchListings(g, server, all->search.id, chunk,
        [this, g, server, all, progress, done, token, count = static_cast<int>(chunk.size())]
        (const QVector<Listing>& listings, const QString& error) {
            if (!error.isEmpty()) {
                done(*all, error);
                return;
            }
            all->fetched += count;
            all->listings += listings;
            if (progress) {
                PriceProgress step;
                step.listings = listings;
                step.fetched = all->fetched;
                step.requested = all->requested;
                step.total = all->total;
                progress(step);
            }
            fetchFirstPricePage(g, server, all, progress, done, token);
        }, token);
}

QVector<Listing> TradeClient::parseFetchListings(const QJsonArray& res)
{
    QVector<Listing> listings;
    for (const auto& v : res) {
        const QJsonObject l = v.toObject().value(QStringLiteral("listing")).toObject();
        const QJsonObject item = v.toObject().value(QStringLiteral("item")).toObject();
        const QJsonObject price = l.value(QStringLiteral("price")).toObject();
        if (price.isEmpty())
            continue;
        Listing x;
        x.amount = price.value(QStringLiteral("amount")).toDouble();
        x.currency = price.value(QStringLiteral("currency")).toString();
        x.account = l.value(QStringLiteral("account")).toObject().value(QStringLiteral("name")).toString();
        x.indexed = parseTime(l.value(QStringLiteral("indexed")).toString());
        x.icon = item.value(QStringLiteral("icon")).toString();
        x.itemName = item.value(QStringLiteral("name")).toString();
        x.baseType = item.value(QStringLiteral("baseType")).toString();
        x.typeLine = item.value(QStringLiteral("typeLine")).toString();
        x.frameType = item.value(QStringLiteral("frameType")).toInt(-1);
        x.ilvl = item.value(QStringLiteral("ilvl")).toInt(-1);
        x.stack = item.value(QStringLiteral("stackSize")).toInt(0);
        x.dps = item.value(QStringLiteral("extended")).toObject().value(QStringLiteral("dps")).toDouble(0);
        x.corrupted = item.value(QStringLiteral("corrupted")).toBool(false);
        x.fractured = item.value(QStringLiteral("fractured")).toBool(false);
        x.synthesised = item.value(QStringLiteral("synthesised")).toBool(false);
        x.mirrored = item.value(QStringLiteral("duplicated")).toBool(false) ||
                     item.value(QStringLiteral("mirrored")).toBool(false);
        x.split = item.value(QStringLiteral("split")).toBool(false);
        x.identified = item.value(QStringLiteral("identified")).toBool(true);
        x.enchantMods = toStringList(item.value(QStringLiteral("enchantMods")));
        x.implicitMods = toStringList(item.value(QStringLiteral("implicitMods")));
        x.fracturedMods = toStringList(item.value(QStringLiteral("fracturedMods")));
        x.explicitMods = toStringList(item.value(QStringLiteral("explicitMods")));
        x.craftedMods = toStringList(item.value(QStringLiteral("craftedMods")));
        x.runeMods = toStringList(item.value(QStringLiteral("runeMods")));
        if (!x.fracturedMods.isEmpty())
            x.fractured = true;
        for (const auto& pv : item.value(QStringLiteral("properties")).toArray()) {
            const QJsonObject p = pv.toObject();
            const QString pn = p.value(QStringLiteral("name")).toString();
            if (p.value(QStringLiteral("type")).toInt(-1) == 6 || pn == QStringLiteral("Quality") ||
                pn == QStringLiteral("퀄리티")) {
                const QJsonArray vals = p.value(QStringLiteral("values")).toArray();
                if (!vals.isEmpty()) {
                    double q = 0;
                    if (text::firstNumber(vals[0].toArray().at(0).toString(), &q))
                        x.quality = static_cast<int>(q);
                }
            }
        }
        const QJsonValue on = l.value(QStringLiteral("account")).toObject().value(QStringLiteral("online"));
        if (on.isObject())
            x.online = on.toObject().value(QStringLiteral("status")).toString() == QStringLiteral("afk") ? 2 : 1;
        else
            x.online = 0;
        if (x.amount > 0)
            listings.push_back(x);
    }
    return listings;
}

void TradeClient::fetchListings(Game g, Lang server, const QString& searchId, const QStringList& ids,
                                std::function<void(const QVector<Listing>&, const QString&)> done, quint64 token)
{
    get(Policy::Fetch, endpoints::fetch(g, server, ids, searchId),
        [done](int, const QByteArray& body, const QString& error) {
            if (!error.isEmpty()) {
                done({}, error);
                return;
            }
            const QJsonValue result = QJsonDocument::fromJson(body).object().value(QStringLiteral("result"));
            if (!result.isArray()) {
                done({}, QStringLiteral("매물 응답 형식이 올바르지 않습니다"));
                return;
            }
            done(parseFetchListings(result.toArray()), QString());
        }, token);
}

void TradeClient::exchange(Game g, Lang server, const QString& league, const QString& have, const QString& want,
                           std::function<void(const ExchangeResult&)> cb, quint64 token)
{
    QJsonObject ex;
    ex.insert(QStringLiteral("status"), QJsonObject{{QStringLiteral("option"), QStringLiteral("online")}});
    ex.insert(QStringLiteral("have"), QJsonArray{have});
    ex.insert(QStringLiteral("want"), QJsonArray{want});
    const QJsonObject body{{QStringLiteral("exchange"), ex}};

    post(Policy::Exchange, endpoints::exchange(g, server, league), QJsonDocument(body).toJson(QJsonDocument::Compact),
         [cb](int, const QByteArray& data, const QString& error) {
             ExchangeResult r;
             r.error = error;
             if (error.isEmpty()) {
                 const QJsonObject o = QJsonDocument::fromJson(data).object();
                 r.id = o.value(QStringLiteral("id")).toString();
                 r.total = o.value(QStringLiteral("total")).toInt();
                 // 현행 응답: result 가 { id: {listing:{offers:[...]}} } 딕셔너리
                 QJsonArray rows;
                 const QJsonValue res = o.value(QStringLiteral("result"));
                 if (res.isObject()) {
                     const QJsonObject ro = res.toObject();
                     for (auto it = ro.begin(); it != ro.end(); ++it)
                         rows.append(it.value());
                 } else if (res.isArray()) {
                     rows = res.toArray();
                 }
                 for (const auto& v : rows) {
                     const QJsonObject l = v.toObject().value(QStringLiteral("listing")).toObject();
                     const QString account = l.value(QStringLiteral("account")).toObject().value(QStringLiteral("name")).toString();
                     const QDateTime indexed = parseTime(l.value(QStringLiteral("indexed")).toString());
                     const QJsonValue on = l.value(QStringLiteral("account")).toObject().value(QStringLiteral("online"));
                     const int online =
                         on.isObject() ? (on.toObject().value(QStringLiteral("status")).toString() == QStringLiteral("afk") ? 2 : 1) : 0;
                     for (const auto& ov : l.value(QStringLiteral("offers")).toArray()) {
                         const QJsonObject off = ov.toObject();
                         const QJsonObject pay = off.value(QStringLiteral("exchange")).toObject();
                         const QJsonObject get = off.value(QStringLiteral("item")).toObject();
                         Listing x;
                         x.account = account;
                         x.indexed = indexed;
                         x.online = online;
                         x.payAmount = pay.value(QStringLiteral("amount")).toDouble();
                         x.getAmount = get.value(QStringLiteral("amount")).toDouble();
                         x.stock = get.value(QStringLiteral("stock")).toInt();
                         x.currency = get.value(QStringLiteral("currency")).toString();
                         x.amount = x.payAmount > 0 ? x.getAmount / x.payAmount : 0; // have 1개당 받는 want
                         if (x.payAmount > 0 && x.getAmount > 0)
                             r.listings.push_back(x);
                     }
                 }
             }
             cb(r);
         },
         token);
}

} // namespace pts
