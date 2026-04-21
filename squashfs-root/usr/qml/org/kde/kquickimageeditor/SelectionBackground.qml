/* SPDX-FileCopyrightText: 2021 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami

Shape {
    id: root
    property bool enableAnimation: !(root.parent instanceof SelectionTool)
        || !(parent.selectionArea.drag.active
            || (parent.pressedHandle && parent.pressedHandle.drag.active))
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.margins: -1
        color: "transparent"
        border.color: Kirigami.Theme.highlightColor
        border.width: 1
    }
    ShapePath {
        id: shapePath
        fillColor: "transparent"
        strokeWidth: 1
        strokeColor: "white"
        strokeStyle: ShapePath.DashLine
        // for some reason, +2 makes the spacing and dash lengths the same, no matter what the strokeWidth is.
        dashPattern: [Kirigami.Units.smallSpacing / strokeWidth, Kirigami.Units.smallSpacing / strokeWidth + 2]
        dashOffset: 0
        startX: -strokeWidth/2; startY: startX
        PathLine { x: root.width - shapePath.startX; y: shapePath.startY }
        PathLine { x: root.width - shapePath.startX; y: root.height - shapePath.startY }
        PathLine { x: shapePath.startX; y: root.height - shapePath.startY }
        PathLine { x: shapePath.startX; y: shapePath.startY }
        NumberAnimation on dashOffset {
            running: root.enableAnimation
            loops: Animation.Infinite
            from: shapePath.dashOffset; to: shapePath.dashOffset + shapePath.dashPattern[0] + shapePath.dashPattern[1]
            duration: 1000
        }
    }
}
