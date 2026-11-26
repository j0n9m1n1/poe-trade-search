// SPDX-License-Identifier: GPL-3.0-or-later
// Windows 아닌 환경(개발/테스트용). 게임 감지/키 주입 없음.
#include "platform/KeyNames.h"
#include "platform/Platform.h"

#include <QGuiApplication>
#include <QWidget>

namespace pts::platform {

GameWindow foregroundGame()
{
    // 개발 환경에선 항상 "게임 창 활성"으로 취급해서 클립보드 감시가 돌게 함
    GameWindow w;
    w.found = true;
    w.foreground = qEnvironmentVariableIsSet("PTS_STUB_FOREGROUND") ? qEnvironmentVariableIntValue("PTS_STUB_FOREGROUND") != 0 : true;
    w.game = qEnvironmentVariable("PTS_STUB_GAME") == QStringLiteral("poe2") ? Game::Poe2 : Game::Poe1;
    w.title = QStringLiteral("stub");
    return w;
}

void focusGame() {}
void sendCopy(bool) {}
void sendKeyToGame(int) {}
void sendChat(const QString&, bool) {}

void setNoActivate(QWidget* w, bool on)
{
    if (w)
        w->setAttribute(Qt::WA_ShowWithoutActivating, on);
}

void activateWindow(QWidget* w)
{
    if (w) {
        w->raise();
        w->activateWindow();
    }
}

bool isAdministrator()
{
    return false;
}

bool mouseButtonDown()
{
    // 전역 마우스 상태를 못 읽음 -> 우리 창 위 클릭만 앎
    return QGuiApplication::mouseButtons() != Qt::NoButton;
}

int qtKeyToVk(int qtKey)
{
    return keys::qtKeyToVk(qtKey);
}

QString vkName(int vk)
{
    return keys::vkName(vk);
}

} // namespace pts::platform
