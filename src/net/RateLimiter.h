// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHash>
#include <QVector>
#include <functional>

namespace pts {

// GGG 거래소 X-Rate-Limit-* 헤더 기반 스로틀러.
//   X-Rate-Limit-Rules: Ip
//   X-Rate-Limit-Ip: 5:10:60,15:60:300,30:300:1800   (최대:기간초:패널티초)
//   X-Rate-Limit-Ip-State: 1:10:0,1:60:0,6:300:0     (현재:기간초:제재남은초)
// 원본은 이걸 무시해서 연타하면 IP 밴 먹었음.
class RateLimiter {
public:
    enum class Policy { Search = 0, Fetch = 1, Exchange = 2, Data = 3 };

    RateLimiter();

    // 지금 보내도 되면 0, 아니면 기다려야 할 ms
    qint64 waitMs(Policy p, qint64 nowMs) const;
    void record(Policy p, qint64 nowMs);
    // header(name) 로 응답 헤더를 읽는다
    void update(Policy p, const std::function<QByteArray(const QByteArray&)>& header, qint64 nowMs);
    void block(Policy p, qint64 untilMs);

private:
    struct Rule {
        int max = 0;
        int period = 0; // 초
    };
    struct State {
        QVector<Rule> rules;
        QVector<qint64> times;
        qint64 blockedUntil = 0;
    };
    State& st(Policy p) { return m_state[static_cast<int>(p)]; }
    const State& st(Policy p) const { return m_state[static_cast<int>(p)]; }

    State m_state[4];
};

} // namespace pts
