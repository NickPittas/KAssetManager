/* SPDX-FileCopyrightText: 2023 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import org.kde.kquickimageeditor
import 'private' as P

Loader {
    id: root
    required property AnnotationViewport viewport
    readonly property AnnotationDocument document: viewport.document
    // Used when we explicitly want to hide the outline
    property bool hidden: false
    // This will be frequently shown and hidden when using the selection tool
    active: visible && document.tool.type === AnnotationTool.SelectTool
    visible: enabled
    sourceComponent: P.DashedOutline {
        id: outline
        // Not animated because of scaling/flickering issues when the path becomes empty
        visible: !root.hidden && !root.viewport.hoveredMousePath.empty
            && root.viewport.hoveredMousePath.boundingRect !== root.document.selectedItem.mousePath.boundingRect
        // These shapes can be complex and don't need to synchronize with any other visuals,
        // so they don't need to be synchronous.
        asynchronous: true
        svgPath: root.viewport.hoveredMousePath.svgPath
        strokeWidth: Utils.clamp(Utils.dprRound(1, Screen.devicePixelRatio),
                                 1 / Screen.devicePixelRatio) / Utils.combinedScale(root.document.transform) / root.viewport.scale
        strokeColor: palette.text
        transformOrigin: Item.TopLeft
        transform: Matrix4x4 {
            matrix: root.document.renderTransform
        }
    }
}
