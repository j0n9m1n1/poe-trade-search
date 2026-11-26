// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/HotkeyManager.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace pts {

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager()
{
    clear();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool HotkeyManager::add(int id, int modifiers, int vk)
{
    if (m_ids.contains(id))
        remove(id);
#ifdef Q_OS_WIN
    if (vk <= 0)
        return false;
    if (!RegisterHotKey(nullptr, id, static_cast<UINT>(modifiers) | MOD_NOREPEAT, static_cast<UINT>(vk)))
        return false;
    m_ids.insert(id);
    return true;
#else
    Q_UNUSED(modifiers);
    Q_UNUSED(vk);
    return false;
#endif
}

void HotkeyManager::remove(int id)
{
#ifdef Q_OS_WIN
    if (m_ids.contains(id))
        UnregisterHotKey(nullptr, id);
#endif
    m_ids.remove(id);
}

void HotkeyManager::clear()
{
    const QSet<int> ids = m_ids;
    for (int id : ids)
        remove(id);
}

bool HotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_HOTKEY) {
            const int id = static_cast<int>(msg->wParam);
            if (m_ids.contains(id)) {
                emit activated(id);
                return true;
            }
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}

} // namespace pts
