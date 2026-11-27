// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/RateLimiter.h"

#include <QList>
#include <algorithm>

namespace pts {

RateLimiter::RateLimiter()
{
    // 헤더 받기 전 기본값 (2026-11 실측값)
    const QVector<Rule> search = {{5, 10}, {15, 60}, {30, 300}};
    st(Policy::Search).rules = search;
    st(Policy::Exchange).rules = search;
    st(Policy::Fetch).rules = {{12, 4}, {16, 12}, {50, 300}};
    st(Policy::Data).rules = {{5, 10}};
}

qint64 RateLimiter::waitMs(Policy p, qint64 now) const
{
    const State& s = st(p);
    qint64 wait = qMax<qint64>(0, s.blockedUntil - now);
    for (const Rule& r : s.rules) {
        if (r.max <= 0 || r.period <= 0)
            continue;
        const qint64 window = static_cast<qint64>(r.period) * 1000;
        QVector<qint64> inWindow; // 오름차순
        for (qint64 t : s.times) {
            if (now - t < window)
                inWindow.push_back(t);
        }
        if (inWindow.size() >= r.max) {
            // 이번 요청이 들어갈 자리를 만들려면 (size - max) 번째 요청이 창 밖으로 나가야 함 (+여유 250ms)
            const qint64 t = inWindow[inWindow.size() - r.max];
            wait = qMax(wait, t + window - now + 250);
        }
    }
    return wait;
}

void RateLimiter::record(Policy p, qint64 now)
{
    State& s = st(p);
    s.times.push_back(now);
    int maxPeriod = 0;
    for (const Rule& r : s.rules)
        maxPeriod = qMax(maxPeriod, r.period);
    const qint64 keep = static_cast<qint64>(qMax(maxPeriod, 1)) * 1000;
    while (!s.times.isEmpty() && now - s.times.first() > keep)
        s.times.removeFirst();
}

void RateLimiter::update(Policy p, const std::function<QByteArray(const QByteArray&)>& header, qint64 now)
{
    const QByteArray rulesHdr = header("X-Rate-Limit-Rules");
    if (rulesHdr.isEmpty())
        return;
    State& s = st(p);
    QVector<Rule> rules;
    for (const QByteArray& name : rulesHdr.split(',')) {
        const QByteArray n = name.trimmed();
        if (n.isEmpty())
            continue;
        const QList<QByteArray> defs = header("X-Rate-Limit-" + n).split(',');
        const QList<QByteArray> states = header("X-Rate-Limit-" + n + "-State").split(',');
        for (int i = 0; i < defs.size(); ++i) {
            const QList<QByteArray> d = defs[i].split(':');
            if (d.size() < 2)
                continue;
            Rule r{d[0].toInt(), d[1].toInt()};
            if (r.max <= 0 || r.period <= 0)
                continue;
            rules.push_back(r);
            if (i < states.size()) {
                const QList<QByteArray> x = states[i].split(':');
                if (x.size() >= 3) {
                    const int cur = x[0].toInt();
                    const int restricted = x[2].toInt();
                    if (restricted > 0) {
                        s.blockedUntil = qMax(s.blockedUntil, now + static_cast<qint64>(restricted) * 1000);
                    } else if (cur >= r.max) {
                        // 창 안 로컬 요청 기록이 서버 집계(cur) 이상이면 waitMs() 가 가장 오래된 요청 만료까지만 대기.
                        // 앱 시작 전이나 브라우저 요청 등으로 로컬 기록이 부족할 때만 보수적으로 전체 주기 동안 대기.
                        const qint64 window = static_cast<qint64>(r.period) * 1000;
                        int inWindow = 0;
                        for (qint64 t : s.times) {
                            if (now - t < window)
                                ++inWindow;
                        }
                        if (inWindow < cur)
                            s.blockedUntil = qMax(s.blockedUntil, now + window);
                    }
                }
            }
        }
    }
    if (!rules.isEmpty())
        s.rules = rules;
}

void RateLimiter::block(Policy p, qint64 untilMs)
{
    State& s = st(p);
    s.blockedUntil = qMax(s.blockedUntil, untilMs);
}

} // namespace pts
