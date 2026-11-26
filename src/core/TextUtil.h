// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVector>

namespace pts::text {

// 숫자 하나 또는 '#' 자리
struct Token {
    bool hash = false;
    double value = 0.0;
};

// 숫자(부호 포함)와 '#'을 전부 '#'으로 바꾸고, 줄마다 공백 정리 후 소문자로.
// 거래소 stat 텍스트와 클립보드 옵션 줄을 같은 키로 맞추는 용도.
QString normalizeStat(const QString& s);

// 숫자 / '#' 토큰을 순서대로 뽑는다. '-'만 부호로 취급 ('+'는 버림).
QVector<Token> tokens(const QString& s);

// <<set:MS>> 같은 태그 제거
QString stripSetTags(const QString& s);

// 카드/화폐 설명 텍스트의 <size:30>{...} 같은 서식 제거
QString stripMarkup(const QString& s);

// Ctrl+Alt+C 고급 설명의 "+52(40-59)" -> "+52"
QString stripAdvancedRanges(const QString& s);

// 고급 설명 줄의 숫자마다 붙은 범위. stripAdvancedRanges() 한 텍스트의 tokens() 와 순서가 같음
struct NumRange {
    bool has = false;
    double lo = 0, hi = 0;
};
QVector<NumRange> numberRanges(const QString& raw);

// "— Unscalable Value" 같은 꼬리 제거
QString stripDashSuffix(const QString& s);

// 숫자만 남김 ("+20% (augmented)" -> "20")
QString digitsOnly(const QString& s);

// 첫 번째 숫자 파싱 (없으면 nullopt)
bool firstNumber(const QString& s, double* out);

// 공백/대소문자 무시 비교용 키
QString looseKey(const QString& s);

} // namespace pts::text
