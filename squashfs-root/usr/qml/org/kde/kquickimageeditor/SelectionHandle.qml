/* SPDX-FileCopyrightText: 2021 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

import QtQuick
import QtQuick.Effects
import org.kde.kirigami as Kirigami

MouseArea {
    id: root
    enum Position {
        TopLeft,    Top,    TopRight,
        Left,               Right,
        BottomLeft, Bottom, BottomRight,
        NPositions
    }
    required property Item target
    property int position: SelectionHandle.TopLeft

    readonly property bool leftSide: position === SelectionHandle.TopLeft
        || position === SelectionHandle.Left
        || position === SelectionHandle.BottomLeft
    readonly property bool rightSide: position === SelectionHandle.TopRight
        || position === SelectionHandle.Right
        || position === SelectionHandle.BottomRight
    readonly property bool topSide: position === SelectionHandle.TopLeft
        || position === SelectionHandle.Top
        || position === SelectionHandle.TopRight
    readonly property bool bottomSide: position === SelectionHandle.BottomLeft
        || position === SelectionHandle.Bottom
        || position === SelectionHandle.BottomRight
    readonly property bool horizontalOnly: position === SelectionHandle.Left || position === SelectionHandle.Right
    readonly property bool verticalOnly: position === SelectionHandle.Top || position === SelectionHandle.Bottom
    // Like forward slash
    readonly property bool forwardDiagonal: position === SelectionHandle.TopRight || position === SelectionHandle.BottomLeft
    // Like backward slash
    readonly property bool backwardDiagonal: position === SelectionHandle.TopLeft || position === SelectionHandle.BottomRight

    property bool lockX: false
    property bool lockY: false

    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true
    anchors.horizontalCenter: if (!pressed && !lockX) {
        if (leftSide) {
            target.left
        } else if (verticalOnly) {
            target.horizontalCenter
        } else {
            target.right
        }
    }
    anchors.verticalCenter: if (!pressed && !lockY) {
        if (topSide) {
            target.top
        } else if (horizontalOnly) {
            target.verticalCenter
        } else {
            target.bottom
        }
    }
    implicitWidth: graphics.implicitWidth + Kirigami.Units.largeSpacing * 2
    implicitHeight: graphics.implicitHeight + Kirigami.Units.largeSpacing * 2
    width: verticalOnly ? target.width - implicitWidth : implicitWidth
    height: horizontalOnly ? target.height - implicitHeight : implicitHeight
    cursorShape: if (horizontalOnly) {
        Qt.SizeHorCursor
    } else if (verticalOnly) {
        Qt.SizeVerCursor
    } else if (forwardDiagonal) {
        // actually oriented like forward slash
        Qt.SizeBDiagCursor
    } else {
        // actually oriented like backward slash
        Qt.SizeFDiagCursor
    }
    drag {
        axis: if (horizontalOnly) {
            Drag.XAxis
        } else if (verticalOnly) {
            Drag.YAxis
        } else {
            Drag.XAndYAxis
        }
        target: pressed ? root : null
        minimumX: -width / 2
        maximumX: parent.width - width / 2
        minimumY: -height / 2
        maximumY: parent.height - height / 2
        threshold: 0
    }
    Rectangle {
        id: graphics
        visible: false
        implicitWidth: Kirigami.Units.gridUnit + Kirigami.Units.gridUnit % 2
        implicitHeight: Kirigami.Units.gridUnit + Kirigami.Units.gridUnit % 2
        anchors.centerIn: parent
        color: Kirigami.Theme.highlightColor
        radius: height / 2
    }
    // Has to be the same size as source
    Item {
        id: maskSource
        visible: false
        anchors.fill: graphics
        Rectangle {
            x: root.leftSide ? parent.width - width : 0
            y: root.topSide ? parent.height - height : 0
            width: root.forwardDiagonal || root.backwardDiagonal || root.horizontalOnly ? parent.width / 2 : parent.width
            height: root.forwardDiagonal || root.backwardDiagonal || root.verticalOnly ? parent.height / 2 : parent.height
        }
    }
    MultiEffect {
        anchors.fill: graphics
        maskInverted: true
        source: graphics
        maskEnabled: true
        maskSource: ShaderEffectSource {
            sourceItem: maskSource
        }
    }
}
