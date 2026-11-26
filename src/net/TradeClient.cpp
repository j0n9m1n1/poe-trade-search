// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/TradeClient.h"

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
    }
}

void TradeClient::pump()
{
    if (m_running || m_queue.isEmpty())
        return;
    const qint64 wait = m_limiter.waitMs(m_queue.head().policy, nowMs());
    if (wait > 0) {
        if (wait > 1500)
            emit throttled(static_cast<int>((wait + 999) / 1000));
        m_timer.start(static_cast<int>(qMin<qint64>(wait, 60 * 60 * 1000)));
        return;
    }
    start(m_queue.dequeue());
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
                err = status == 429 ? QStringLiteral("요청이 너무 많음 (레이트 리밋)") : reply->errorString();
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
                     r.error = QStringLiteral("검색 응답이 이상함");
             }
             cb(r);
         },
         token);
}

void TradeClient::priceCheck(Game g, Lang server, const QString& league, const QJsonObject& query, int count,
                             std::function<void(const PriceProgress&)> progress,
                             std::function<void(const PriceProgress&, const QString&)> done, quint64 token)
{
    search(
        g, server, league, query,
        [this, g, server, count, progress, done, token](const SearchResult& sr) {
            auto all = std::make_shared<PriceProgress>();
            if (!sr.error.isEmpty()) {
                done(*all, sr.error);
                return;
            }
            all->total = sr.total;
            const QStringList ids = sr.ids.mid(0, count);
            all->requested = ids.size();
            if (ids.isEmpty()) {
                done(*all, QString());
                return;
            }
            const int batches = (ids.size() + 9) / 10;
            auto remaining = std::make_shared<int>(batches);
            auto failed = std::make_shared<QString>();
            for (int b = 0; b < batches; ++b) {
                const QStringList chunk = ids.mid(b * 10, 10);
                get(
                    Policy::Fetch, endpoints::fetch(g, server, chunk, sr.id),
                    [all, remaining, failed, progress, done, chunk](int, const QByteArray& body, const QString& error) {
                        PriceProgress step;
                        if (!error.isEmpty()) {
                            *failed = error;
                        } else {
                            const QJsonArray res =
                                QJsonDocument::fromJson(body).object().value(QStringLiteral("result")).toArray();
                            for (const auto& v : res) {
                                const QJsonObject l = v.toObject().value(QStringLiteral("listing")).toObject();
                                const QJsonObject price = l.value(QStringLiteral("price")).toObject();
                                if (price.isEmpty())
                                    continue;
                                Listing x;
                                x.amount = price.value(QStringLiteral("amount")).toDouble();
                                x.currency = price.value(QStringLiteral("currency")).toString();
                                x.account = l.value(QStringLiteral("account")).toObject().value(QStringLiteral("name")).toString();
                                x.indexed = parseTime(l.value(QStringLiteral("indexed")).toString());
                                if (x.amount > 0)
                                    step.listings.push_back(x);
                            }
                        }
                        all->listings += step.listings;
                        all->fetched += chunk.size();
                        step.fetched = all->fetched;
                        step.requested = all->requested;
                        step.total = all->total;
                        if (progress)
                            progress(step);
                        if (--(*remaining) == 0)
                            done(*all, all->listings.isEmpty() ? *failed : QString());
                    },
                    token);
            }
        },
        token);
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
                     for (const auto& ov : l.value(QStringLiteral("offers")).toArray()) {
                         const QJsonObject off = ov.toObject();
                         const QJsonObject pay = off.value(QStringLiteral("exchange")).toObject();
                         const QJsonObject get = off.value(QStringLiteral("item")).toObject();
                         Listing x;
                         x.account = account;
                         x.indexed = indexed;
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
