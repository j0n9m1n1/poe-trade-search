// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;

namespace pts {

// 아이템 아이콘 디스크 캐시
//   <dir>/index.json : 아이템 키 -> 이미지 URL (fetch 결과에서 배움)
//   <dir>/<sha1(url)>.png
// 아이콘은 web.poecdn.com CDN 이라 거래소 레이트 리밋과 별개
class IconCache : public QObject {
    Q_OBJECT
public:
    explicit IconCache(const QString& dir, QObject* parent = nullptr);

    static QString absoluteUrl(const QString& urlOrPath); // "/gen/image/..." -> https://web.poecdn.com/...

    QString urlFor(const QString& key) const { return m_index.value(key); }
    void remember(const QString& key, const QString& url);

    QString cachedFile(const QString& url) const; // 있으면 경로, 없으면 빈 문자열
    // 캐시에 있으면 바로, 없으면 받아서 cb(경로). 실패 시 cb("")
    void get(const QString& url, std::function<void(const QString& path)> cb);

private:
    QString fileFor(const QString& url) const;
    void saveIndex() const;

    QString m_dir;
    QHash<QString, QString> m_index;
    QNetworkAccessManager* m_nam;
    QHash<QString, QList<std::function<void(const QString&)>>> m_pending;
};

} // namespace pts
