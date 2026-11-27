// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QPair>
#include <QString>

namespace pts::ui {

// 설정 창과 팝업의 판매 방식 콤보를 같은 목록·문구로 통일
inline QList<QPair<QString, QString>> tradeStatusChoices()
{
    return {
        {QStringLiteral("즉시 구입만"), QStringLiteral("securable")},
        {QStringLiteral("즉시 구입 + 직접 거래"), QStringLiteral("available")},
        {QStringLiteral("직접 거래 (리그 접속 중)"), QStringLiteral("onlineleague")},
        {QStringLiteral("직접 거래 (접속 중)"), QStringLiteral("online")},
        {QStringLiteral("모두"), QStringLiteral("any")},
    };
}

} // namespace pts::ui
