// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Types.h"

#include <QString>
#include <functional>

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
bool gameRunsElevated();
bool startElevated(const QString& executable, const QString& arguments, QString* error);

// 실행 중 표시용 이름 있는 뮤텍스 (설치 프로그램 AppMutex=PoeTradeSearchQt_Running 이 확인)
void createInstanceMutex();

// 지금 마우스 버튼(좌/우/가운데)이 눌려 있는지 (전역). 팝업 바깥 클릭 감지용
bool mouseButtonDown();

enum class GameClickTarget { OutsideGame, GameArea, Game };
GameClickTarget cursorGameClickTarget();

// 팝업 밖 버튼 누름의 위치를 누른 시점에 판정. 등록 실패 시 호출자가 폴링으로 대체.
bool watchOutsideMousePress(QWidget* popup, std::function<void(GameClickTarget)> onPress);
void stopWatchingOutsideMousePress();

// Qt 키 -> Windows VK (단축키 입력 상자용). Windows 에선 nativeVirtualKey 를 우선 씀
int qtKeyToVk(int qtKey);
QString vkName(int vk);

} // namespace pts::platform
