/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import QtQuick.Shapes
import org.kde.kquickimageeditor

Shape {
    id: root
    property alias strokeWidth: shapePath.strokeWidth
    // The stroke color beneath the dash
    property alias strokeColor: shapePath.strokeColor
    property alias strokeStyle: shapePath.strokeStyle
    property alias capStyle: shapePath.capStyle
    property alias joinStyle: shapePath.joinStyle
    property alias svgPath: pathSvg.path
    property alias pathScale: shapePath.scale
    property alias pathHints: shapePath.pathHints

    // Get a rectangular SVG path
    function rectanglePath(x, y, w, h) {
        // absolute start at top-left,
        // relative line to top-right,
        // relative line to bottom-right
        // relative line to bottom-left
        // close path (automatic line to top-left)
        return `M ${x},${y}
                l ${w},0
                l 0,${h}
                l ${-w},0
                z`
    }

    function ratio(dividend, divisor) {
        return !Number.isFinite(dividend) || !Number.isFinite(divisor) || !dividend || !divisor ?
            0 : dividend / divisor
    }
    function unTranslateScale(oldValue, scale) {
        return oldValue - oldValue * scale
    }

    // Get a matrix4x4 that moves the stroke outside the bounds of the path
    function outerStrokeScaleValue(originalValue, strokeWidth = root.strokeWidth) {
        return ratio(originalValue + strokeWidth * 2, originalValue + strokeWidth)
    }

    // Get a matrix4x4 that moves the stroke outside the bounds of the path
    function outerStrokeTranslateValue(originalValue, scale, strokeWidth = root.strokeWidth) {
        return unTranslateScale(originalValue, scale) - strokeWidth / 2
    }

    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        id: shapePath
        function dprRound(v) {
            return Math.round(v * Screen.devicePixelRatio) / Screen.devicePixelRatio
        }
        fillColor: "transparent"
        // ensure outline is always thick enough to be visible, but grows with zoom
        strokeWidth: dprRound(1)
        strokeColor: palette.highlight
        // Solid line because it's easier to do the alternating color effect this way.
        strokeStyle: ShapePath.SolidLine
        joinStyle: ShapePath.MiterJoin
        PathSvg {
            id: pathSvg
            path: rectanglePath(strokeWidth / 2, strokeWidth / 2,
                                width - strokeWidth, height - strokeWidth)
        }
    }
}
