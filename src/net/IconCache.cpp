// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/IconCache.h"

#include "core/JsonUtil.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace pts {

IconCache::IconCache(const QString& dir, QObject* parent)
    : QObject(parent), m_dir(dir), m_nam(new QNetworkAccessManager(this))
{
    QDir().mkpath(m_dir);
    QJsonDocument doc;
    if (json::readFile(QDir(m_dir).filePath(QStringLiteral("index.json")), &doc)) {
        const QJsonObject o = doc.object();
        for (auto it = o.begin(); it != o.end(); ++it)
            m_index.insert(it.key(), it.value().toString());
    }
}

QString IconCache::absoluteUrl(const QString& u)
{
    if (u.startsWith(QStringLiteral("http://")) || u.startsWith(QStringLiteral("https://")))
        return u;
    if (u.startsWith(QLatin1Char('/')))
        return QStringLiteral("https://web.poecdn.com") + u;
    return {};
}

void IconCache::remember(const QString& key, const QString& url)
{
    const QString abs = absoluteUrl(url);
    if (key.isEmpty() || abs.isEmpty() || m_index.value(key) == abs)
        return;
    m_index.insert(key, abs);
    saveIndex();
}

void IconCache::saveIndex() const
{
    QJsonObject o;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it)
        o.insert(it.key(), it.value());
    json::writeFile(QDir(m_dir).filePath(QStringLiteral("index.json")), QJsonDocument(o), false, false);
}

QString IconCache::fileFor(const QString& url) const
{
    const QByteArray h = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(m_dir).filePath(QString::fromLatin1(h) + QStringLiteral(".png"));
}

QString IconCache::cachedFile(const QString& url) const
{
    const QString abs = absoluteUrl(url);
    if (abs.isEmpty())
        return {};
    const QString f = fileFor(abs);
    return QFileInfo::exists(f) ? f : QString();
}

void IconCache::get(const QString& url, std::function<void(const QString&)> cb)
{
    const QString abs = absoluteUrl(url);
    if (abs.isEmpty()) {
        cb(QString());
        return;
    }
    const QString f = fileFor(abs);
    if (QFileInfo::exists(f)) {
        cb(f);
        return;
    }
    auto& waiters = m_pending[abs];
    waiters.push_back(std::move(cb));
    if (waiters.size() > 1)
        return; // 이미 받는 중

    QNetworkRequest req{QUrl(abs)};
    req.setTransferTimeout(10000);
    QNetworkReply* r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, abs, f]() {
        r->deleteLater();
        QString path;
        const QByteArray data = r->readAll();
        if (r->error() == QNetworkReply::NoError && data.startsWith("\x89PNG") && json::writeRaw(f, data))
            path = f;
        const auto cbs = m_pending.take(abs);
        for (const auto& c : cbs)
            c(path);
    });
}

} // namespace pts
