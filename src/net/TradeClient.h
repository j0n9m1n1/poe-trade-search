// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/PriceSummary.h"
#include "core/Types.h"
#include "net/RateLimiter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace pts {

// 거래소 API 클라이언트. 요청은 한 줄로 세워서 레이트 리밋 지키며 하나씩 보냄.
class TradeClient : public QObject {
    Q_OBJECT
public:
    using Policy = RateLimiter::Policy;
    using RawCallback = std::function<void(int status, const QByteArray& body, const QString& error)>;

    explicit TradeClient(QObject* parent = nullptr);

    void setTimeoutSec(int s) { m_timeoutSec = s; }
    void setUserAgent(const QString& ua) { m_userAgent = ua; }

    // token: cancel() 로 한 번에 취소할 묶음 번호
    quint64 newToken() { return ++m_lastToken; }
    void cancel(quint64 token);

    void get(Policy p, const QUrl& url, RawCallback cb, quint64 token = 0);
    void post(Policy p, const QUrl& url, const QByteArray& json, RawCallback cb, quint64 token = 0);

    // ---- 고수준
    struct SearchResult {
        QString id;
        QStringList ids;
        int total = 0;
        QString error;
    };
    void search(Game g, Lang server, const QString& league, const QJsonObject& query,
                std::function<void(const SearchResult&)> cb, quint64 token = 0);

    struct PriceProgress {
        QVector<Listing> listings; // 이번에 새로 받은 것
        SearchResult search; // 완료 응답에서 이어서 불러올 검색 결과
        int fetched = 0;
        int requested = 0;
        int total = 0;
    };
    // 검색 -> fetch(10개씩) 까지
    void priceCheck(Game g, Lang server, const QString& league, const QJsonObject& query, int count,
                    std::function<void(const PriceProgress&)> progress,
                    std::function<void(const PriceProgress& all, const QString& error)> done, quint64 token);
    void fetchListings(Game g, Lang server, const QString& searchId, const QStringList& ids,
                       std::function<void(const QVector<Listing>&, const QString&)> done, quint64 token);
    // fetch 응답의 result 배열 -> Listing 목록 (툴팁용 아이템 요약 포함)
    static QVector<Listing> parseFetchListings(const QJsonArray& res);

    struct ExchangeResult {
        QString id;
        QVector<Listing> listings;
        int total = 0;
        QString error;
    };
    static ExchangeResult parseExchangeReply(const QByteArray& data);
    void exchange(Game g, Lang server, const QString& league, const QString& have, const QString& want,
                  std::function<void(const ExchangeResult&)> cb, quint64 token = 0);

    qint64 waitMs(Policy p) const;

signals:
    void throttled(int waitSeconds);

private:
    struct Job {
        Policy policy;
        QUrl url;
        QByteArray body;
        bool isPost = false;
        RawCallback cb;
        quint64 token = 0;
    };
    void pump();
    void start(const Job& j);
    void fetchFirstPricePage(Game g, Lang server, const std::shared_ptr<PriceProgress>& all,
                             std::function<void(const PriceProgress&)> progress,
                             std::function<void(const PriceProgress&, const QString&)> done, quint64 token);

    QNetworkAccessManager* m_nam;
    RateLimiter m_limiter;
    QQueue<Job> m_queue;
    QPointer<QNetworkReply> m_running;
    Job m_runningJob;
    QTimer m_timer;
    int m_timeoutSec = 10;
    QString m_userAgent;
    quint64 m_lastToken = 0;
};

} // namespace pts
