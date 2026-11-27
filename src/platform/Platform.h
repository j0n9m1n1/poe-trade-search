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
// 채팅 명령 1개: Enter(또는 Ctrl+Enter=마지막 귓말 답장) -> 텍스트 -> Enter
void sendChat(const QString& text, bool replyToLastWhisper);

// 포커스 안 뺏는 팝업 (WS_EX_NOACTIVATE)
void setNoActivate(QWidget* w, bool on);
// 입력칸 클릭했을 때 진짜로 활성화
void activateWindow(QWidget* w);

bool isAdministrator();

// 실행 중 표시용 이름 있는 뮤텍스 (설치 프로그램 AppMutex=PoeTradeSearchQt_Running 이 확인)
void createInstanceMutex();

// 지금 마우스 버튼(좌/우/가운데)이 눌려 있는지 (전역). 팝업 바깥 클릭 감지용
bool mouseButtonDown();

// 마우스 커서가 PoE 게임 화면 위인지 (창 모드에서 다른 프로그램을 누른 건 무시하려고)
//  게임 창 영역 안이고, 그 위에 떠 있는 창이 PoE 거나 "항상 위" 창(오버레이류)이면 게임 화면으로 봄
//  detail 에는 판정 근거 (로그용)
bool cursorOverGame(QString* detail = nullptr);

// Qt 키 -> Windows VK (단축키 입력 상자용). Windows 에선 nativeVirtualKey 를 우선 씀
int qtKeyToVk(int qtKey);
QString vkName(int vk);

} // namespace pts::platform
