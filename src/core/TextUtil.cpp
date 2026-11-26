// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/TextUtil.h"

#include <QRegularExpression>
#include <QStringList>

namespace pts::text {

namespace {
const QRegularExpression& numOrHashRe()
{
    static const QRegularExpression re(QStringLiteral("[+\\-]?(?:\\d+(?:\\.\\d+)?|#)"));
    return re;
}
} // namespace

QString normalizeStat(const QString& s)
{
    QString t = s;
    t.replace(numOrHashRe(), QStringLiteral("#"));
    const QStringList lines = t.split(QLatin1Char('\n'));
    QStringList out;
    out.reserve(lines.size());
    for (const QString& l : lines)
        out << l.simplified().toLower();
    return out.join(QLatin1Char('\n'));
}

QVector<Token> tokens(const QString& s)
{
    QVector<Token> out;
    auto it = numOrHashRe().globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        QString v = m.captured(0);
        Token t;
        if (v.endsWith(QLatin1Char('#'))) {
            t.hash = true;
        } else {
            if (v.startsWith(QLatin1Char('+')))
                v.remove(0, 1);
            t.value = v.toDouble();
        }
        out.push_back(t);
    }
    return out;
}

QString stripSetTags(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("<<[^>]*>>"));
    QString t = s;
    t.remove(re);
    return t.trimmed();
}

QString stripMarkup(const QString& s)
{
    static const QRegularExpression tag(QStringLiteral("<[a-zA-Z]+(?::[0-9]+)?>"));
    QString t = s;
    t.remove(tag);
    t.remove(QLatin1Char('{'));
    t.remove(QLatin1Char('}'));
    return t;
}

QString stripAdvancedRanges(const QString& s)
{
    // 52(40-59), 1.5(1.2-1.8), -10(-15--5), 25(20~30)
    static const QRegularExpression re(QStringLiteral(
        "(\\d+(?:\\.\\d+)?)\\(\\s*[+\\-]?\\d+(?:\\.\\d+)?\\s*(?:[-~–—]\\s*[+\\-]?\\d+(?:\\.\\d+)?)?\\s*\\)"));
    QString t = s;
    t.replace(re, QStringLiteral("\\1"));
    return t;
}

QVector<NumRange> numberRanges(const QString& raw)
{
    static const QRegularExpression re(QStringLiteral(
        "[+\\-]?\\d+(?:\\.\\d+)?(?:\\(\\s*([+\\-]?\\d+(?:\\.\\d+)?)\\s*(?:[-~–—]\\s*([+\\-]?\\d+(?:\\.\\d+)?))?\\s*\\))?"));
    QVector<NumRange> out;
    auto it = re.globalMatch(raw);
    while (it.hasNext()) {
        const auto m = it.next();
        NumRange r;
        if (m.capturedLength(1) > 0) {
            r.has = true;
            r.lo = m.captured(1).toDouble();
            r.hi = m.capturedLength(2) > 0 ? m.captured(2).toDouble() : r.lo;
            if (r.lo > r.hi)
                std::swap(r.lo, r.hi);
        }
        out.push_back(r);
    }
    return out;
}

QString stripDashSuffix(const QString& s)
{
    static const QRegularExpression re(QStringLiteral("\\s+—\\s+.*$"));
    QString t = s;
    t.remove(re);
    return t;
}

QString digitsOnly(const QString& s)
{
    QString out;
    for (const QChar c : s) {
        if (c.isDigit())
            out += c;
        else if (!out.isEmpty() && c == QLatin1Char('('))
            break; // "20 (Max)" 같은 꼬리는 버림
    }
    return out;
}

bool firstNumber(const QString& s, double* out)
{
    static const QRegularExpression re(QStringLiteral("-?\\d+(?:\\.\\d+)?"));
    const auto m = re.match(s);
    if (!m.hasMatch())
        return false;
    if (out)
        *out = m.captured(0).toDouble();
    return true;
}

QString looseKey(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (!c.isSpace() && c != QLatin1Char('-'))
            out += c.toLower();
    }
    return out;
}

} // namespace pts::text
