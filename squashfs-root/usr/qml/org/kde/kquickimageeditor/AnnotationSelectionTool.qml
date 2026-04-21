/*
 *  SPDX-FileCopyrightText: 2022 Marco Martin <mart@kde.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import org.kde.kquickimageeditor
import 'private' as P

AnimatedLoader {
    id: root
    required property AnnotationViewport viewport
    readonly property AnnotationDocument document: viewport.document
    readonly property bool shouldShow: enabled
        && document.tool.type === AnnotationTool.SelectTool
        && document.selectedItem.hasSelection
        && (document.selectedItem.options & AnnotationTool.TextOption) === 0
    property bool dragging: root.shouldShow ? (item?.dragging ?? false) : false
    property bool hovered: root.shouldShow ? (item?.hovered ?? false) : false

    state: shouldShow ? "active" : "inactive"

    sourceComponent: Item {
        readonly property bool dragging: tlHandle.dragging || tHandle.dragging || trHandle.dragging
                                      || lHandle.dragging || rHandle.dragging
                                      || blHandle.dragging || bHandle.dragging || brHandle.dragging
        readonly property bool hovered: tlHandle.hovered || tHandle.hovered || trHandle.hovered
                                     || lHandle.hovered || rHandle.hovered
                                     || blHandle.hovered || bHandle.hovered || brHandle.hovered

        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true

        focus: true

        P.DashedOutline {
            id: outline
            svgPath: root.document.selectedItem.mousePath.svgPath
            // Invisible when empty because of scaling/flickering issues when the path becomes empty
            visible: !root.document.selectedItem.mousePath.empty
            strokeWidth: Utils.clamp(Utils.dprRound(1, Screen.devicePixelRatio),
                                     1 / Screen.devicePixelRatio) / Utils.combinedScale(root.document.transform) / root.viewport.scale
            transformOrigin: Item.TopLeft
            transform: Matrix4x4 {
                matrix: root.document.renderTransform
            }
            containsMode: Outline.FillContains
            HoverHandler {
                cursorShape: Qt.SizeAllCursor
            }
        }

        component ResizeHandle: P.Handle {
            id: handle
            readonly property alias dragging: dragHandler.active
            readonly property alias hovered: hoverHandler.hovered

            // For visibility when the outline is not very rectangular
            strokeWidth: 1 / Screen.devicePixelRatio / root.viewport.scale
            x: relativeXForEdges(parent, edges)
                + xOffsetForEdges(strokeWidth, edges)
            y: relativeYForEdges(parent, edges)
                + yOffsetForEdges(strokeWidth, edges)

            visible: root.document.selectedItem.hasSelection
                && (root.document.selectedItem.options & AnnotationTool.NumberOption) === 0
            enabled: visible

            HoverHandler {
                id: hoverHandler
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
                property point lastDocumentPos
                property int effectiveEdges: 0
                margin: Math.min(handle.parent.width, handle.parent.height) < 12 ? 0 : 4
                target: null
                dragThreshold: 0
                onActiveTranslationChanged: if (active && lastDocumentPos !== undefined) {
                    // We use the difference between the current document position
                    // and a stored document press position instead of activeTranslation
                    // so that the real translation can be tracked regardless of how the view is scaled.
                    let documentMousePos = Utils.sceneToDocumentPoint(centroid.scenePosition, root.viewport)
                    // We want the relative mouse movement since the last onActiveTranslationChanged
                    const map = Utils.handleResizeProperties(documentMousePos.x - lastDocumentPos.x,
                                                             documentMousePos.y - lastDocumentPos.y,
                                                             effectiveEdges,
                                                             root.document)
                    effectiveEdges = map.edges
                    root.document.selectedItem.applyTransform(map.matrix)
                    lastDocumentPos = documentMousePos;
                }
                // DragHandler::activeChanged is emitted before DragHandler::translationChanged.
                // HandlerPoint is completely updated, so we don't want to do onCentroidChanged.
                // Otherwise, we would update our document press position when we don't want to.
                onActiveChanged: if (active) {
                    lastDocumentPos = Utils.sceneToDocumentPoint(centroid.scenePressPosition, root.viewport)
                    effectiveEdges = handle.edges
                } else {
                    root.document.selectedItem.commitChanges()
                }
            }
        }
        Item {
            id: resizeHandles
            property rect rect: Qt.rect(0, 0, 0, 0)
            Binding on rect {
                value: root.document.renderTransform.mapRect(root.document.selectedItem.mousePath.boundingRect)
                when: root.shouldShow
                restoreMode: Binding.RestoreNone
            }
            x: rect.x
            y: rect.y
            width: rect.width
            height: rect.height
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
        Component.onCompleted: forceActiveFocus()
    }
}


