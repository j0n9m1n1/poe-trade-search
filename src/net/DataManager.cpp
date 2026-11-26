// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/DataManager.h"

#include "core/JsonUtil.h"
#include "core/Keywords.h"
#include "net/Endpoints.h"
#include "net/TradeClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>

namespace pts {

DataManager::DataManager(const QString& dataDir, TradeClient* client, QObject* parent)
    : QObject(parent), m_dataDir(dataDir), m_client(client)
{
}

QStringList DataManager::kinds()
{
    return {QStringLiteral("leagues"), QStringLiteral("stats"), QStringLiteral("items"), QStringLiteral("static"),
            QStringLiteral("filters")};
}

QString DataManager::cacheDir(Game g, Lang l) const
{
    return QDir(m_dataDir).filePath(QStringLiteral("cache/%1/%2").arg(gameKey(g), langKey(l)));
}

bool DataManager::hasCache(Game g) const
{
    for (Lang l : {Lang::Ko, Lang::En}) {
        if (QFileInfo::exists(QDir(cacheDir(g, l)).filePath(QStringLiteral("stats.json"))))
            return true;
    }
    return false;
}

QDateTime DataManager::updatedAt(Game g) const
{
    QDateTime best;
    for (Lang l : {Lang::Ko, Lang::En}) {
        QFile f(QDir(cacheDir(g, l)).filePath(QStringLiteral("updated.txt")));
        if (f.open(QIODevice::ReadOnly)) {
            const QDateTime t = QDateTime::fromString(QString::fromUtf8(f.readAll()).trimmed(), Qt::ISODate);
            if (t.isValid() && (!best.isValid() || t > best))
                best = t;
        }
    }
    return best;
}

bool DataManager::isStale(Game g, int maxAgeDays) const
{
    if (!hasCache(g))
        return true;
    const QDateTime t = updatedAt(g);
    return !t.isValid() || (maxAgeDays > 0 && t.daysTo(QDateTime::currentDateTimeUtc()) >= maxAgeDays);
}

void DataManager::update(Game g, std::function<void(bool, const QString&)> done)
{
    if (m_updating[gi(g)]) {
        if (done)
            done(false, QStringLiteral("이미 업데이트 중"));
        return;
    }
    m_updating[gi(g)] = true;
    for (Lang l : {Lang::Ko, Lang::En})
        QDir(cacheDir(g, l) + QStringLiteral(".new")).removeRecursively();

    struct Job {
        Lang lang;
        QString kind;
    };
    QVector<Job> jobs;
    for (Lang l : {Lang::En, Lang::Ko}) {
        for (const QString& k : kinds())
            jobs.push_back({l, k});
    }
    auto remaining = std::make_shared<int>(jobs.size());
    auto okLang = std::make_shared<QHash<int, int>>(); // lang -> 성공 개수
    auto lastError = std::make_shared<QString>();
    const quint64 token = m_client->newToken();

    emit progress(QStringLiteral("%1 거래소 데이터 받는 중...").arg(g == Game::Poe1 ? QStringLiteral("PoE1") : QStringLiteral("PoE2")));

    for (const Job& j : jobs) {
        const QString dir = cacheDir(g, j.lang);
        // 임시 폴더에 받고 다 되면 교체
        const QString tmpDir = dir + QStringLiteral(".new");
        m_client->get(
            TradeClient::Policy::Data, endpoints::data(g, j.lang, j.kind),
            [this, g, j, dir, tmpDir, remaining, okLang, lastError, done, total = int(jobs.size())](
                int status, const QByteArray& body, const QString& error) {
                bool ok = error.isEmpty() && status == 200 &&
                          QJsonDocument::fromJson(body).object().contains(QStringLiteral("result"));
                if (ok) {
                    QString werr;
                    ok = json::writeRaw(QDir(tmpDir).filePath(j.kind + QStringLiteral(".json")), body, &werr);
                    if (!ok)
                        *lastError = werr;
                } else {
                    *lastError = QStringLiteral("%1 %2: %3")
                                     .arg(langKey(j.lang), j.kind, error.isEmpty() ? QString::number(status) : error);
                }
                if (ok)
                    (*okLang)[li(j.lang)]++;
                emit progress(QStringLiteral("데이터 받는 중 %1/%2").arg(total - *remaining + 1).arg(total));

                if (--(*remaining) > 0)
                    return;

                // 한 언어 단위로 전부 성공한 것만 교체
                bool any = false;
                for (Lang l : {Lang::Ko, Lang::En}) {
                    const QString d = cacheDir(g, l);
                    const QString t = d + QStringLiteral(".new");
                    if (okLang->value(li(l)) == kinds().size()) {
                        QDir(d).removeRecursively();
                        QDir().rename(t, d);
                        json::writeRaw(QDir(d).filePath(QStringLiteral("updated.txt")),
                                       QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
                        any = true;
                    } else {
                        QDir(t).removeRecursively();
                    }
                }
                m_updating[gi(g)] = false;
                if (done)
                    done(any, any ? QString() : *lastError);
            },
            token);
    }
}

bool DataManager::load(Game g, GameDb* out, const Keywords& kw, QString* error) const
{
    bool any = false;
    QString err;
    for (Lang l : {Lang::En, Lang::Ko}) {
        QString e;
        if ((*out)[l].load(cacheDir(g, l), &e))
            any = true;
        else if (!e.isEmpty())
            err = e;
    }
    (*out)[Lang::En].resolveScopes(kw, nullptr);
    (*out)[Lang::Ko].resolveScopes(kw, out->loaded(Lang::En) ? &(*out)[Lang::En] : nullptr);
    if (!any && error)
        *error = err;
    return any;
}

} // namespace pts
