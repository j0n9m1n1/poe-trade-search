// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/HotkeyEdit.h"

#include "platform/Platform.h"

#include <QKeyEvent>

namespace pts {

HotkeyEdit::HotkeyEdit(QWidget* parent) : QLineEdit(parent)
{
    setReadOnly(true);
    setPlaceholderText(QStringLiteral("없음"));
    setToolTip(QStringLiteral("누른 키 조합이 저장됨. Delete/Backspace/Esc 는 지우기"));
}

QString HotkeyEdit::describe(int vk, int mods)
{
    if (vk <= 0)
        return {};
    QString s;
    if (mods & 2)
        s += QStringLiteral("Ctrl + ");
    if (mods & 4)
        s += QStringLiteral("Shift + ");
    if (mods & 1)
        s += QStringLiteral("Alt + ");
    if (mods & 8)
        s += QStringLiteral("Win + ");
    return s + platform::vkName(vk);
}

void HotkeyEdit::setHotkey(int vk, int modifiers)
{
    m_vk = vk;
    m_mods = vk > 0 ? modifiers : 0;
    setText(describe(m_vk, m_mods));
}

void HotkeyEdit::keyPressEvent(QKeyEvent* e)
{
    const int k = e->key();
    const Qt::KeyboardModifiers qm = e->modifiers();
    int mods = 0;
    if (qm & Qt::AltModifier)
        mods |= 1;
    if (qm & Qt::ControlModifier)
        mods |= 2;
    if (qm & Qt::ShiftModifier)
        mods |= 4;
    if (qm & Qt::MetaModifier)
        mods |= 8;

    if (mods == 0 && (k == Qt::Key_Delete || k == Qt::Key_Backspace || k == Qt::Key_Escape)) {
        setHotkey(0, 0);
        emit hotkeyChanged();
        return;
    }
    if (k == Qt::Key_Control || k == Qt::Key_Shift || k == Qt::Key_Alt || k == Qt::Key_Meta || k == Qt::Key_AltGr ||
        k == Qt::Key_unknown)
        return;

    int vk = 0;
#ifdef Q_OS_WIN
    vk = static_cast<int>(e->nativeVirtualKey());
#endif
    if (vk == 0)
        vk = platform::qtKeyToVk(k);
    if (vk == 0)
        return;
    setHotkey(vk, mods);
    emit hotkeyChanged();
}

} // namespace pts
