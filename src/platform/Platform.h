// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Types.h"

#include <QString>

class QWidget;

namespace pts::platform {

struct GameWindow {
    bool found = false;
    bool foreground = false;
    Game game = Game::Poe1; // 창 제목 "Path of Exile 2" 면 Poe2
    QString title;
};

// 현재 포그라운드 창이 PoE(클래스 POEWindowClass)인지
GameWindow foregroundGame();

// PoE 창을 앞으로
void focusGame();

// 키 입력 주입 (Windows SendInput)
void sendCopy(bool advanced);       // Ctrl+C / Ctrl+Alt+C
void sendKeyToGame(int virtualKey); // ESC 넘겨주기 등

// 포커스 안 뺏는 팝업 (WS_EX_NOACTIVATE)
void setNoActivate(QWidget* w, bool on);
// 입력칸 클릭했을 때 진짜로 활성화
void activateWindow(QWidget* w);

bool isAdministrator();

// Qt 키 -> Windows VK (단축키 입력 상자용). Windows 에선 nativeVirtualKey 를 우선 씀
int qtKeyToVk(int qtKey);
QString vkName(int vk);

} // namespace pts::platform
