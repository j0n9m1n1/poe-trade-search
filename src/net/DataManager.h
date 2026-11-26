// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/TradeDb.h"
#include "core/Types.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

namespace pts {

class Keywords;
class TradeClient;

// 거래소 데이터(stats/items/static/filters/leagues)를 게임 x 언어별로 받아서 캐시
//   <dataDir>/cache/<poe1|poe2>/<ko|en>/<what>.json
class DataManager : public QObject {
    Q_OBJECT
public:
    DataManager(const QString& dataDir, TradeClient* client, QObject* parent = nullptr);

    static QStringList kinds();
    static QUrl matcherUrl(Game g, Lang l);
    QString cacheDir(Game g, Lang l) const;
    bool hasCache(Game g) const;
    QDateTime updatedAt(Game g) const;
    bool isStale(Game g, int maxAgeDays) const;

    // 한/영 둘 다 받음. 한쪽만 성공해도 ok=true (카카오가 죽어 있을 수 있음)
    void update(Game g, std::function<void(bool ok, const QString& error)> done);
    bool isUpdating(Game g) const { return m_updating[gi(g)]; }

    bool load(Game g, GameDb* out, const Keywords& kw, QString* error = nullptr) const;

signals:
    void progress(const QString& message);

private:
    QString m_dataDir;
    TradeClient* m_client;
    bool m_updating[2] = {false, false};
};

} // namespace pts
