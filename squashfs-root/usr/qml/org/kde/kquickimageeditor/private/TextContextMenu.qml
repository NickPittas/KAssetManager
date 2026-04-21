/* SPDX-FileCopyrightText: 2025 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC

QQC.Menu {
    id: root
    required property Item target
    popupType: QQC.Popup.Native
    QQC.MenuItem {
        action: QQC.Action {
            icon.name: "edit-undo-symbolic"
            text: i18nc("@action:inmenu", "Undo")
            enabled: root.target?.canUndo ?? false
            onTriggered: root.target.undo()
        }
    }
    QQC.MenuItem {
        action: QQC.Action {
            icon.name: "edit-redo-symbolic"
            text: i18nc("@action:inmenu", "Redo")
            enabled: root.target?.canRedo ?? false
            onTriggered: root.target.redo()
        }
    }
    QQC.MenuSeparator {}
    QQC.MenuItem {
        action: QQC.Action {
            icon.name: "edit-cut-symbolic"
            text: i18nc("@action:inmenu", "Cut")
            enabled: root.target?.selectedText.length ?? false
            onTriggered: root.target.cut()
        }
    }
    QQC.MenuItem {
        action: QQC.Action {
            icon.name: "edit-copy-symbolic"
            text: i18nc("@action:inmenu", "Copy")
            enabled: root.target?.selectedText.length ?? false
            onTriggered: root.target.copy()
        }
    }
    QQC.MenuItem {
        action: QQC.Action {
            icon.name: "edit-paste-symbolic"
            text: i18nc("@action:inmenu", "Paste")
            enabled: root.target?.canPaste ?? false
            onTriggered: root.target.paste()
        }
    }
    QQC.MenuItem {
        action: QQC.Action {
            icon.name: "edit-delete-symbolic"
            text: i18nc("@action:inmenu", "Delete")
            enabled: root.target?.selectedText.length ?? false
            onTriggered: root.target.remove(root.target.selectionStart, root.target.selectionEnd)
        }
    }
    QQC.MenuSeparator {}
    QQC.MenuItem {
        action: QQC.Action {
            icon.name: "edit-select-all-symbolic"
            text: i18nc("@action:inmenu", "Select All")
            enabled: root.target?.length ?? false
            onTriggered: root.target.selectAll()
        }
    }
}
