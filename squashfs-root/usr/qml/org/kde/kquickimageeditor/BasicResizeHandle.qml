/*
 * SPDX-FileCopyrightText: (C) 2020 Carl Schwan <carl@carlschwan.eu>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

import QtQuick

import org.kde.kirigami as Kirigami
import org.kde.kquickimageeditor as KQuickImageEditor

KQuickImageEditor.ResizeHandle {
    width: Kirigami.Settings.isMobile ? 20 : 10
    height: width
    Kirigami.ShadowedRectangle {
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        color: Kirigami.Theme.backgroundColor
        shadow {
            size: 4
            color: Kirigami.Theme.textColor
        }
        anchors.fill: parent
        radius: width
        opacity: 0.8
    }
    scale: 1 
}
