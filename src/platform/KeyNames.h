// SPDX-License-Identifier: GPL-3.0-or-later
// Windows VK <-> Qt 키 / 이름 (플랫폼 공통)
#pragma once

#include <Qt>
#include <QString>

namespace pts::platform::keys {

inline int qtKeyToVk(int k)
{
    if (k >= Qt::Key_A && k <= Qt::Key_Z)
        return 0x41 + (k - Qt::Key_A);
    if (k >= Qt::Key_0 && k <= Qt::Key_9)
        return 0x30 + (k - Qt::Key_0);
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24)
        return 0x70 + (k - Qt::Key_F1);
    switch (k) {
    case Qt::Key_Escape: return 0x1B;
    case Qt::Key_Tab: return 0x09;
    case Qt::Key_Backspace: return 0x08;
    case Qt::Key_Return:
    case Qt::Key_Enter: return 0x0D;
    case Qt::Key_Space: return 0x20;
    case Qt::Key_Insert: return 0x2D;
    case Qt::Key_Delete: return 0x2E;
    case Qt::Key_Home: return 0x24;
    case Qt::Key_End: return 0x23;
    case Qt::Key_PageUp: return 0x21;
    case Qt::Key_PageDown: return 0x22;
    case Qt::Key_Left: return 0x25;
    case Qt::Key_Up: return 0x26;
    case Qt::Key_Right: return 0x27;
    case Qt::Key_Down: return 0x28;
    case Qt::Key_Pause: return 0x13;
    case Qt::Key_ScrollLock: return 0x91;
    case Qt::Key_Minus: return 0xBD;
    case Qt::Key_Equal: return 0xBB;
    case Qt::Key_BracketLeft: return 0xDB;
    case Qt::Key_BracketRight: return 0xDD;
    case Qt::Key_Semicolon: return 0xBA;
    case Qt::Key_Apostrophe: return 0xDE;
    case Qt::Key_Comma: return 0xBC;
    case Qt::Key_Period: return 0xBE;
    case Qt::Key_Slash: return 0xBF;
    case Qt::Key_Backslash: return 0xDC;
    case Qt::Key_QuoteLeft: return 0xC0;
    default: return 0;
    }
}

inline QString vkName(int vk)
{
    if (vk >= 0x41 && vk <= 0x5A)
        return QString(QChar(vk));
    if (vk >= 0x30 && vk <= 0x39)
        return QString(QChar(vk));
    if (vk >= 0x70 && vk <= 0x87)
        return QStringLiteral("F%1").arg(vk - 0x70 + 1);
    if (vk >= 0x60 && vk <= 0x69)
        return QStringLiteral("Num%1").arg(vk - 0x60);
    switch (vk) {
    case 0x1B: return QStringLiteral("Esc");
    case 0x09: return QStringLiteral("Tab");
    case 0x08: return QStringLiteral("Backspace");
    case 0x0D: return QStringLiteral("Enter");
    case 0x20: return QStringLiteral("Space");
    case 0x2D: return QStringLiteral("Insert");
    case 0x2E: return QStringLiteral("Delete");
    case 0x24: return QStringLiteral("Home");
    case 0x23: return QStringLiteral("End");
    case 0x21: return QStringLiteral("PageUp");
    case 0x22: return QStringLiteral("PageDown");
    case 0x25: return QStringLiteral("Left");
    case 0x26: return QStringLiteral("Up");
    case 0x27: return QStringLiteral("Right");
    case 0x28: return QStringLiteral("Down");
    case 0x13: return QStringLiteral("Pause");
    case 0x91: return QStringLiteral("ScrollLock");
    case 0xC0: return QStringLiteral("`");
    case 0xBD: return QStringLiteral("-");
    case 0xBB: return QStringLiteral("=");
    case 0xDB: return QStringLiteral("[");
    case 0xDD: return QStringLiteral("]");
    case 0xBA: return QStringLiteral(";");
    case 0xDE: return QStringLiteral("'");
    case 0xBC: return QStringLiteral(",");
    case 0xBE: return QStringLiteral(".");
    case 0xBF: return QStringLiteral("/");
    case 0xDC: return QStringLiteral("\\");
    case 0: return QString();
    default: return QStringLiteral("0x%1").arg(vk, 2, 16, QLatin1Char('0'));
    }
}

} // namespace pts::platform::keys
