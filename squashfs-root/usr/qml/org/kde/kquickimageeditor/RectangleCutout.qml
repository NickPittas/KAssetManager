/* SPDX-FileCopyrightText: 2021 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

import QtQuick

Item {
    id: root
    property color color: Qt.rgba(0,0,0,0.5)
    property real insideX: 0
    property real insideY: 0
    property real insideWidth: width
    property real insideHeight: height
    // NOTE: Using 4 Rectangles is visibly smoother than using an OpacityMask effect
    Rectangle {
        id: topTintRect
        LayoutMirroring.enabled: false
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.insideY
        color: root.color
    }

    Rectangle {
        id: leftTintRect
        LayoutMirroring.enabled: false
        anchors.left: parent.left
        anchors.top: topTintRect.bottom
        anchors.bottom: bottomTintRect.top
        width: root.insideX
        color: root.color
    }

    Rectangle {
        id: rightTintRect
        LayoutMirroring.enabled: false
        anchors.right: parent.right
        anchors.top: topTintRect.bottom
        anchors.bottom: bottomTintRect.top
        width: root.width - root.insideX - root.insideWidth
        color: root.color
    }

    Rectangle {
        id: bottomTintRect
        LayoutMirroring.enabled: false
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.height - root.insideY - root.insideHeight
        color: root.color
    }
}
