// SPDX-License-Identifier: GPL-3.0-or-later
// Windows 구현 (원본 Functions.cs Native 클래스 대응)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "platform/KeyNames.h"
#include "platform/Platform.h"

#include <QDir>
#include <QWidget>
#include <string>
#include <utility>
#include <vector>

namespace pts::platform {

namespace {

const wchar_t* kPoeClass = L"POEWindowClass";

QString windowTitle(HWND h)
{
    wchar_t buf[256] = {};
    const int n = GetWindowTextW(h, buf, 255);
    return QString::fromWCharArray(buf, n > 0 ? n : 0);
}

bool isPoeWindow(HWND h)
{
    if (!h)
        return false;
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 63);
    return wcscmp(cls, kPoeClass) == 0;
}

Game gameFromTitle(const QString& t)
{
    return t.contains(QStringLiteral("Path of Exile 2")) ? Game::Poe2 : Game::Poe1;
}

void pushKey(std::vector<INPUT>& v, WORD vk, bool up)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    v.push_back(in);
}

bool isDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

GameClickTarget gameClickTarget(POINT point, HWND hitWindow)
{
    if (isPoeWindow(hitWindow))
        return GameClickTarget::Game;

    const HWND foreground = GetForegroundWindow();
    const HWND game = isPoeWindow(foreground) ? foreground : FindWindowW(kPoeClass, nullptr);
    RECT bounds = {};
    if (!game || !GetWindowRect(game, &bounds) || !PtInRect(&bounds, point))
        return GameClickTarget::OutsideGame;
    if (hitWindow && (GetWindowLongPtrW(hitWindow, GWL_EXSTYLE) & WS_EX_TOPMOST))
        return GameClickTarget::Game; // 게임 영역 위의 항상 위 오버레이
    return GameClickTarget::GameArea;
}

HHOOK outsideMouseHook = nullptr;
HWND watchedPopup = nullptr;
std::function<void(GameClickTarget)> outsideMousePress;

LRESULT CALLBACK onMouseInput(int code, WPARAM message, LPARAM data)
{
    if (code == HC_ACTION && outsideMousePress &&
        (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN)) {
        const POINT point = reinterpret_cast<const MSLLHOOKSTRUCT*>(data)->pt;
        const HWND hitWindow = GetAncestor(WindowFromPoint(point), GA_ROOT);
        if (hitWindow != watchedPopup) {
            const auto target = gameClickTarget(point, hitWindow);
            if (target != GameClickTarget::OutsideGame)
                outsideMousePress(target);
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}

} // namespace

GameWindow foregroundGame()
{
    GameWindow w;
    HWND fg = GetForegroundWindow();
    if (isPoeWindow(fg)) {
        w.found = true;
        w.foreground = true;
        w.title = windowTitle(fg);
        w.game = gameFromTitle(w.title);
        return w;
    }
    HWND any = FindWindowW(kPoeClass, nullptr);
    if (any) {
        w.found = true;
        w.title = windowTitle(any);
        w.game = gameFromTitle(w.title);
    }
    return w;
}

void focusGame()
{
    if (HWND h = FindWindowW(kPoeClass, nullptr))
        SetForegroundWindow(h);
}

void sendCopy(bool advanced)
{
    std::vector<INPUT> v;
    // 단축키 누르느라 잡고 있는 Shift/Win(+고급이 아니면 Alt)은 떼고 보냄
    for (int vk : {VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN}) {
        if (isDown(vk))
            pushKey(v, static_cast<WORD>(vk), true);
    }
    if (!advanced) {
        for (int vk : {VK_LMENU, VK_RMENU}) {
            if (isDown(vk))
                pushKey(v, static_cast<WORD>(vk), true);
        }
    }
    pushKey(v, VK_CONTROL, false);
    if (advanced)
        pushKey(v, VK_MENU, false);
    pushKey(v, 'C', false);
    pushKey(v, 'C', true);
    if (advanced)
        pushKey(v, VK_MENU, true);
    pushKey(v, VK_CONTROL, true);
    SendInput(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT));
}

void sendKeyToGame(int virtualKey)
{
    std::vector<INPUT> v;
    pushKey(v, static_cast<WORD>(virtualKey), false);
    pushKey(v, static_cast<WORD>(virtualKey), true);
    SendInput(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT));
}

void sendChat(const QString& text, bool replyToLastWhisper)
{
    std::vector<INPUT> v;
    // 단축키 누르느라 잡고 있는 수식키 떼기
    for (int vk : {VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_RMENU, VK_LCONTROL, VK_RCONTROL, VK_LWIN, VK_RWIN}) {
        if (isDown(vk))
            pushKey(v, static_cast<WORD>(vk), true);
    }
    // 채팅창 열기 (Ctrl+Enter = "@마지막귓말상대 " 가 채워진 채로 열림)
    if (replyToLastWhisper)
        pushKey(v, VK_CONTROL, false);
    pushKey(v, VK_RETURN, false);
    pushKey(v, VK_RETURN, true);
    if (replyToLastWhisper)
        pushKey(v, VK_CONTROL, true);
    SendInput(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT));
    Sleep(40);

    v.clear();
    if (!replyToLastWhisper) {
        // 쓰다 만 글이 있으면 덮어쓰기
        pushKey(v, VK_CONTROL, false);
        pushKey(v, 'A', false);
        pushKey(v, 'A', true);
        pushKey(v, VK_CONTROL, true);
    }
    // 유니코드 직접 입력 (한글 IME 상태와 무관)
    auto unicode = [&v](char16_t ch, bool up) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = 0;
        in.ki.wScan = static_cast<WORD>(ch);
        in.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
        v.push_back(in);
    };
    const std::u16string w = text.toStdU16String();
    for (size_t i = 0; i < w.size(); ++i) {
        const char16_t ch = w[i];
        if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < w.size()) {
            // 서로게이트 쌍은 둘 다 누르고 둘 다 뗌
            const char16_t lo = w[++i];
            unicode(ch, false);
            unicode(lo, false);
            unicode(ch, true);
            unicode(lo, true);
        } else {
            unicode(ch, false);
            unicode(ch, true);
        }
    }
    SendInput(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT));
    Sleep(20);

    v.clear();
    pushKey(v, VK_RETURN, false);
    pushKey(v, VK_RETURN, true);
    SendInput(static_cast<UINT>(v.size()), v.data(), sizeof(INPUT));
}

void setNoActivate(QWidget* w, bool on)
{
    if (!w)
        return;
    w->setAttribute(Qt::WA_ShowWithoutActivating, on);
    HWND h = reinterpret_cast<HWND>(w->winId());
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    ex = on ? (ex | WS_EX_NOACTIVATE) : (ex & ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE));
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
}

void activateWindow(QWidget* w)
{
    if (!w)
        return;
    setNoActivate(w, false);
    HWND h = reinterpret_cast<HWND>(w->winId());
    SetForegroundWindow(h);
    w->activateWindow();
}

bool isAdministrator()
{
    BOOL member = FALSE;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = nullptr;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &admins)) {
        CheckTokenMembership(nullptr, admins, &member);
        FreeSid(admins);
    }
    return member != FALSE;
}

bool gameRunsElevated()
{
    const HWND foreground = GetForegroundWindow();
    const HWND game = isPoeWindow(foreground) ? foreground : FindWindowW(kPoeClass, nullptr);
    DWORD pid = 0;
    if (!game || !GetWindowThreadProcessId(game, &pid))
        return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    HANDLE token = nullptr;
    const BOOL opened = OpenProcessToken(process, TOKEN_QUERY, &token);
    CloseHandle(process);
    if (!opened)
        return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const BOOL read = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return read && elevation.TokenIsElevated != 0;
}

bool startElevated(const QString& executable, const QString& arguments, QString* error)
{
    const std::wstring file = QDir::toNativeSeparators(executable).toStdWString();
    const std::wstring params = arguments.toStdWString();
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = file.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info))
        return true;
    const DWORD code = GetLastError();
    if (error && code != ERROR_CANCELLED)
        *error = QString::number(code);
    return false;
}

void createInstanceMutex()
{
    // 프로세스 끝날 때까지 들고 있음 (종료 시 OS 가 정리)
    static HANDLE h = CreateMutexW(nullptr, FALSE, L"PoeTradeSearchQt_Running");
    Q_UNUSED(h);
}

bool mouseButtonDown()
{
    return isDown(VK_LBUTTON) || isDown(VK_RBUTTON) || isDown(VK_MBUTTON);
}

GameClickTarget cursorGameClickTarget()
{
    POINT point;
    if (!GetCursorPos(&point))
        return GameClickTarget::OutsideGame;
    return gameClickTarget(point, GetAncestor(WindowFromPoint(point), GA_ROOT));
}

bool watchOutsideMousePress(QWidget* popup, std::function<void(GameClickTarget)> onPress)
{
    watchedPopup = reinterpret_cast<HWND>(popup->winId());
    outsideMouseHook = SetWindowsHookExW(WH_MOUSE_LL, onMouseInput, GetModuleHandleW(nullptr), 0);
    if (!outsideMouseHook)
        return false;
    outsideMousePress = std::move(onPress);
    return true;
}

void stopWatchingOutsideMousePress()
{
    if (outsideMouseHook)
        UnhookWindowsHookEx(outsideMouseHook);
    outsideMouseHook = nullptr;
    watchedPopup = nullptr;
    outsideMousePress = {};
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
