/* SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>
 * SPDX-FileCopyrightText: 2024 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import org.kde.kquickimageeditor

import 'private' as P

Loader {
    id: root
    required property AnnotationViewport viewport
    readonly property AnnotationDocument document: viewport.document
    readonly property AnnotationTool tool: document?.tool ?? null
    visible: active
    sourceComponent: Item {
        id: baseItem
        Binding {
            target: root.tool
            property: "geometry"
            value: Utils.rectNormalized(baseItem.itemRect(selectionItem))
            when: baseItem.dragging
            restoreMode: Binding.RestoreNone
        }
        readonly property bool dragging: bgDragHandler.active
            || tlHandle.dragging || tHandle.dragging || trHandle.dragging
            || lHandle.dragging || selectionDragHandler.active || rHandle.dragging
            || blHandle.dragging || bHandle.dragging || brHandle.dragging

        function acceptCrop(): void {
            document.cropCanvas(root.tool.geometry)
            root.tool.geometry = undefined
        }
        function itemRect(item: Item): rect {
            return Qt.rect(item.x, item.y, item.width, item.height)
        }
        function setItemRect(item: Item, dimension: rect): void {
            item.x = dimension.x
            item.y = dimension.y
            item.width = dimension.width
            item.height = dimension.height
        }
        function maxRect(): rect {
            return Qt.rect(0, 0, width, height)
        }
        opacity: selectionItem.visible
        Behavior on opacity {
            OpacityAnimator {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.OutCubic
            }
        }
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true

        HoverHandler {
            cursorShape: Qt.CrossCursor
        }
        DragHandler {
            id: bgDragHandler
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            target: null
            dragThreshold: 0
            xAxis {
                minimum: 0
                maximum: baseItem.width
            }
            yAxis {
                minimum: 0
                maximum: baseItem.height
            }
            onTranslationChanged: if (active) {
                const aspectRatio = root.tool.aspectRatio
                const pressPosition = Utils.dprRound(centroid.pressPosition, Screen.devicePixelRatio)
                let w = Utils.dprRound(activeTranslation.x, Screen.devicePixelRatio) / root.viewport.scale
                let h = Utils.dprRound(activeTranslation.y, Screen.devicePixelRatio) / root.viewport.scale
                let rect = Qt.rect(pressPosition.x, pressPosition.y, w, h)
                rect = Utils.rectAspectRatioed(rect, aspectRatio)
                rect = Utils.rectClipped(rect, baseItem.maxRect())
                setItemRect(selectionItem, rect)
            }
        }
        PointHandler {
            id: pointHandler
            acceptedButtons: Qt.AllButtons
            onActiveChanged: if (active) {
                baseItem.forceActiveFocus(Qt.MouseFocusReason)
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onSingleTapped: (eventPoint, button) => {
                root.tool.geometry = undefined
            }
        }
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
                acceptCrop()
                event.accepted = true
            } else if (event.matches(StandardKey.Cancel)) {
                root.tool.geometry = undefined
                event.accepted = true
            }
        }

        Connections {
            target: root.document
            // Clip selection in case someone undos/redos a crop while this tool is active
            function onUndoStackDepthChanged() {
                let rect = Utils.rectAspectRatioed(baseItem.itemRect(selectionItem), aspectRatio)
                rect = Utils.rectClipped(rect, baseItem.maxRect())
                baseItem.setItemRect(selectionItem, rect)
            }
        }

        component Overlay: Rectangle {
            color: "black"
            opacity: 0.5
        }
        Overlay { // top / full overlay when nothing selected
            id: topOverlay
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: root.tool.geometry.top
        }
        Overlay { // bottom
            id: bottomOverlay
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height - root.tool.geometry.bottom
        }
        Overlay { // left
            anchors {
                left: topOverlay.left
                top: topOverlay.bottom
                bottom: bottomOverlay.top
            }
            width: root.tool.geometry.left
        }
        Overlay { // right
            anchors {
                top: topOverlay.bottom
                right: topOverlay.right
                bottom: bottomOverlay.top
            }
            width: parent.width - root.tool.geometry.right
        }

        Item { // Can have negative geometry, so we don't put visuals or handlers in here
            id: selectionItem
            visible: width !== 0 || height !== 0
            Binding on x {
                delayed: true
                value: root.tool.geometry.x
                when: !baseItem.dragging
                restoreMode: Binding.RestoreNone
            }
            Binding on y {
                delayed: true
                value: root.tool.geometry.y
                when: !baseItem.dragging
                restoreMode: Binding.RestoreNone
            }
            Binding on width {
                delayed: true
                value: root.tool.geometry.width
                when: !baseItem.dragging
                restoreMode: Binding.RestoreNone
            }
            Binding on height {
                delayed: true
                value: root.tool.geometry.height
                when: !baseItem.dragging
                restoreMode: Binding.RestoreNone
            }
        }

        Outline {
            pathHints: ShapePath.PathLinear
            x: root.tool.geometry.x - strokeWidth
            y: root.tool.geometry.y - strokeWidth
            width: root.tool.geometry.width + strokeWidth * 2
            height: root.tool.geometry.height + strokeWidth * 2
            strokeWidth: Utils.clamp(Utils.dprRound(1, Screen.devicePixelRatio),
                                     1 / Screen.devicePixelRatio) / root.viewport.scale
            strokeColor: if (enabled) {
                return palette.active.highlight
            } else {
                return "black"
            }
            HoverHandler {
                cursorShape: Qt.SizeAllCursor
            }
            DragHandler {
                id: selectionDragHandler
                target: selectionItem
                dragThreshold: 0
                xAxis {
                    minimum: 0
                    maximum: baseItem.width - root.tool.geometry.width
                }
                yAxis {
                    minimum: 0
                    maximum: baseItem.height - root.tool.geometry.height
                }
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: (eventPoint, button) => {
                    acceptCrop()
                }
            }
        }

        component ResizeHandle: P.Handle {
            id: handle
            x: root.tool.geometry.x + relativeXForEdges(root.tool.geometry, edges)
                + xOffsetForEdges(strokeWidth / 2, edges)
            y: root.tool.geometry.y + relativeYForEdges(root.tool.geometry, edges)
                + yOffsetForEdges(strokeWidth / 2, edges)
            visible: selectionItem.visible
            readonly property bool dragging: dragHandler.active

            HoverHandler {
                margin: dragHandler.margin
                cursorShape: {
                    if (enabled) {
                        return handle.cursorShapeForEdges(handle.edges)
                    } else {
                        return undefined
                    }
                }
            }
            DragHandler {
                id: dragHandler
                target: null
                margin: Math.min(root.tool.geometry.width, root.tool.geometry.height) < 12 ? 0 : 4
                dragThreshold: 0
                xAxis {
                    enabled: handle.edges & (Qt.LeftEdge | Qt.RightEdge)
                    minimum: -handle.width / 2
                    maximum: baseItem.width - handle.width / 2
                }
                yAxis {
                    enabled: handle.edges & (Qt.TopEdge | Qt.BottomEdge)
                    minimum: -handle.height / 2
                    maximum: baseItem.height - handle.height / 2
                }
                onTranslationChanged: (delta) => {
                    delta = Utils.dprRound(delta, Screen.devicePixelRatio)
                    if (active && (delta.x !== 0 || delta.y !== 0)) {
                        delta.x /= root.viewport.scale
                        delta.y /= root.viewport.scale
                        let rect = baseItem.itemRect(selectionItem)
                        if (handle.edges & Qt.LeftEdge) {
                            rect.width -= delta.x
                            rect.x += delta.x
                        } else if (handle.edges & Qt.RightEdge) {
                            rect.width += delta.x
                        }
                        if (handle.edges & Qt.TopEdge) {
                            rect.height -= delta.y
                            rect.y += delta.y
                        } else if (handle.edges & Qt.BottomEdge) {
                            rect.height += delta.y
                        }
                        rect = Utils.rectAspectRatioedForHandle(rect, root.tool.aspectRatio, handle.edges)
                        rect = Utils.rectClipped(rect, baseItem.maxRect())
                        setItemRect(selectionItem, rect)
                    }
                }
            }
        }
        ResizeHandle {
            id: tlHandle
            edges: Qt.TopEdge | Qt.LeftEdge
        }
        ResizeHandle {
            id: tHandle
            edges: Qt.TopEdge
        }
        ResizeHandle {
            id: trHandle
            edges: Qt.TopEdge | Qt.RightEdge
        }
        ResizeHandle {
            id: lHandle
            edges: Qt.LeftEdge
        }
        ResizeHandle {
            id: rHandle
            edges: Qt.RightEdge
        }
        ResizeHandle {
            id: blHandle
            edges: Qt.BottomEdge | Qt.LeftEdge
        }
        ResizeHandle {
            id: bHandle
            edges: Qt.BottomEdge
        }
        ResizeHandle {
            id: brHandle
            edges: Qt.BottomEdge | Qt.RightEdge
        }
    }
}
