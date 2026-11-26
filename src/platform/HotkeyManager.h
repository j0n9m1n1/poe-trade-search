// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QSet>

namespace pts {

// 전역 단축키 (Windows RegisterHotKey + WM_HOTKEY). 다른 OS 에선 아무것도 안 함.
class HotkeyManager : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager() override;

    // modifiers: 1=Alt 2=Ctrl 4=Shift 8=Win, vk: Windows VK
    bool add(int id, int modifiers, int vk);
    void remove(int id);
    void clear();
    bool has(int id) const { return m_ids.contains(id); }
    bool isEmpty() const { return m_ids.isEmpty(); }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void activated(int id);

private:
    QSet<int> m_ids;
};

} // namespace pts
