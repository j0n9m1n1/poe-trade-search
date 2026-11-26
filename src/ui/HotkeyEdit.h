// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QLineEdit>

namespace pts {

// 원본 HotkeyBox: 키 조합 캡처. Delete/Backspace/Esc 단독은 지움
class HotkeyEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit HotkeyEdit(QWidget* parent = nullptr);

    void setHotkey(int vk, int modifiers);
    int vk() const { return m_vk; }
    int modifiers() const { return m_mods; }

    static QString describe(int vk, int modifiers);

signals:
    void hotkeyChanged();

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    int m_vk = 0;
    int m_mods = 0;
};

} // namespace pts
