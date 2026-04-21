/* SPDX-FileCopyrightText: 2021 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

import QtQuick
import QtQml

Item {
    id: root
    // make this readonly so it can be accessed without risking external modification
    readonly property SelectionHandle pressedHandle: _private.pressedHandle
    readonly property alias selectionArea: selectionArea
    property alias selectionX: _private.pendingRect.x
    property alias selectionY: _private.pendingRect.y
    property alias selectionWidth: _private.pendingRect.width
    property alias selectionHeight: _private.pendingRect.height
    property int aspectRatio: SelectionTool.AspectRatio.Free

    enum AspectRatio {
        Free,
        Square
    }

    QtObject {
        id: _private

        readonly property rect currentRect: Qt.rect(selectionArea.x, selectionArea.y, selectionArea.width, selectionArea.height)
        property SelectionHandle pressedHandle: null
        property bool applyingPendingRect: false
        property bool updatingPendingRect: false
        property rect pendingRect: currentRect

        onPressedHandleChanged: {
            if (!pressedHandle && !_private.applyingPendingRect) {
                Qt.callLater(_private.updateHandles);
            }
        }

        function isSquareRatio(): bool {
            return root.aspectRatio === SelectionTool.AspectRatio.Square;
        }

        function updateHandles(): void {
            if (_private.applyingPendingRect) {
                return;
            }

            _private.applyingPendingRect = true;
            selectionArea.x = _private.pendingRect.x;
            selectionArea.y = _private.pendingRect.y;
            selectionArea.width = _private.pendingRect.width;
            selectionArea.height = _private.pendingRect.height;
            _private.applyingPendingRect = false;
        }

        function updatePendingRect(): void {
            if (_private.applyingPendingRect || _private.updatingPendingRect) {
                return;
            }

            if ((selectionArea.pressed || _private.pressedHandle)) {
                _private.updatingPendingRect = true;

                if (_private.isSquareRatio()) {
                    const flagX = 0x1;
                    const flagY = 0x2;
                    const flagWidth = 0x4;
                    const flagHeight = 0x8;
                    const oldRect = _private.currentRect;
                    let wide = Math.max(oldRect.width, oldRect.height);
                    let newRect = oldRect;

                    function offset() {
                        if (newRect.x < 0 || newRect.y < 0) {
                            return Math.abs(Math.min(newRect.x, newRect.y));
                        } else if (newRect.x + newRect.width > root.width) {
                            return (newRect.x + newRect.width) - root.width;
                        } else if (newRect.y + newRect.height > root.height) {
                            return (newRect.y + newRect.height) - root.height;
                        }

                        return 0;
                    }

                    function patchValues(flags, offset) {
                        if (flags & flagX) newRect.x += offset;
                        if (flags & flagY) newRect.y += offset;
                        if (flags & flagWidth) newRect.width -= offset;
                        if (flags & flagHeight) newRect.height -= offset;
                    }

                    switch (_private.pressedHandle) {
                    case handleTopLeft:
                        newRect = Qt.rect(oldRect.right - wide, oldRect.bottom - wide, wide, wide);
                        patchValues(flagX | flagY | flagWidth | flagHeight, offset());
                        break;
                    case handleTopRight:
                        newRect = Qt.rect(oldRect.left, oldRect.bottom - wide, wide, wide);
                        patchValues(flagY | flagWidth | flagHeight, offset());
                        break;
                    case handleBottomRight:
                        newRect = Qt.rect(oldRect.left, oldRect.top, wide, wide);
                        patchValues(flagWidth | flagHeight, offset());
                        break;
                    case handleBottomLeft:
                        newRect = Qt.rect(oldRect.right - wide, oldRect.top, wide, wide);
                        patchValues(flagX | flagWidth | flagHeight, offset());
                        break;
                    }

                    if (_private.pendingRect !== newRect) {
                        _private.pendingRect = newRect;
                    }
                } else {
                    _private.pendingRect = _private.currentRect;
                }

                _private.updatingPendingRect = false;
            }
        }
    }

    MouseArea {
        id: selectionArea
        x: 0
        y: 0
        z: 1
        width: parent.width
        height: parent.height
        LayoutMirroring.enabled: false
        anchors.left: if (_private.pressedHandle) {
            if (_private.pressedHandle.backwardDiagonal) {
                handleTopLeft.horizontalCenter
            } else if (_private.pressedHandle.forwardDiagonal) {
                handleBottomLeft.horizontalCenter
            } else if (_private.pressedHandle.horizontalOnly) {
                handleLeft.horizontalCenter
            }
        }
        anchors.right: if (_private.pressedHandle) {
            if (_private.pressedHandle.backwardDiagonal) {
                handleBottomRight.horizontalCenter
            } else if (_private.pressedHandle.forwardDiagonal) {
                handleTopRight.horizontalCenter
            } else if (_private.pressedHandle.horizontalOnly) {
                handleRight.horizontalCenter
            }
        }
        anchors.top: if (_private.pressedHandle) {
            if (_private.pressedHandle.backwardDiagonal) {
                handleTopLeft.verticalCenter
            } else if (_private.pressedHandle.forwardDiagonal) {
                handleTopRight.verticalCenter
            } else if (_private.pressedHandle.verticalOnly) {
                handleTop.verticalCenter
            }
        }
        anchors.bottom: if (_private.pressedHandle) {
            if (_private.pressedHandle.backwardDiagonal) {
                handleBottomRight.verticalCenter
            } else if (_private.pressedHandle.forwardDiagonal) {
                handleBottomLeft.verticalCenter
            } else if (_private.pressedHandle.verticalOnly) {
                handleBottom.verticalCenter
            }
        }
        enabled: drag.target
        cursorShape: if (_private.pressedHandle || (pressed && enabled)) {
            Qt.ClosedHandCursor
        } else if (enabled) {
            Qt.OpenHandCursor
        } else {
            Qt.ArrowCursor
        }
        drag {
            axis: Drag.XAndYAxis
            target: (selectionArea.width === root.width && selectionArea.height === root.height) || _private.pressedHandle ? null : selectionArea
            minimumX: 0
            maximumX: root.width - selectionArea.width
            minimumY: 0
            maximumY: root.height - selectionArea.height
            threshold: 0
        }

        onMouseXChanged: _private.updatePendingRect()
        onMouseYChanged: _private.updatePendingRect()
        onXChanged: _private.updatePendingRect()
        onYChanged: _private.updatePendingRect()
        onWidthChanged: _private.updatePendingRect()
        onHeightChanged: _private.updatePendingRect()
    }

    SelectionHandle {
        id: handleTopLeft
        target: selectionArea
        position: SelectionHandle.TopLeft
        lockX: _private.pressedHandle && _private.pressedHandle.backwardDiagonal
        lockY: lockX
        drag.maximumX: handleBottomRight.x - implicitWidth / 2
        drag.maximumY: handleBottomRight.y - implicitHeight / 2
        Binding {
            target: _private; property: "pressedHandle"
            value: handleTopLeft; when: !_private.applyingPendingRect && handleTopLeft.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    SelectionHandle {
        id: handleTop
        visible: !_private.isSquareRatio() && selectionArea.width >= implicitWidth
        target: selectionArea
        position: SelectionHandle.Top
        lockY: _private.pressedHandle && _private.pressedHandle.verticalOnly
        drag.maximumY: handleBottom.y - implicitHeight / 2
        Binding {
            target: _private; property: "pressedHandle"
            value: handleTop; when: handleTop.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    SelectionHandle {
        id: handleTopRight
        target: selectionArea
        position: SelectionHandle.TopRight
        lockX: _private.pressedHandle && _private.pressedHandle.forwardDiagonal
        lockY: lockX
        drag.minimumX: handleBottomLeft.x + implicitWidth / 2
        drag.maximumY: handleBottomLeft.y - implicitHeight / 2
        Binding {
            target: _private; property: "pressedHandle"
            value: handleTopRight; when: !_private.applyingPendingRect && handleTopRight.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    SelectionHandle {
        id: handleLeft
        visible: !_private.isSquareRatio() && selectionArea.height >= implicitHeight
        target: selectionArea
        position: SelectionHandle.Left
        lockX: _private.pressedHandle && _private.pressedHandle.horizontalOnly
        drag.maximumX: handleRight.x - implicitWidth / 2
        Binding {
            target: _private; property: "pressedHandle"
            value: handleLeft; when: handleLeft.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    SelectionHandle {
        id: handleRight
        visible: !_private.isSquareRatio() && selectionArea.height >= implicitHeight
        target: selectionArea
        position: SelectionHandle.Right
        lockX: _private.pressedHandle && _private.pressedHandle.horizontalOnly
        drag.minimumX: handleLeft.x + implicitWidth / 2
        Binding {
            target: _private; property: "pressedHandle"
            value: handleRight; when: handleRight.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    SelectionHandle {
        id: handleBottomLeft
        target: selectionArea
        position: SelectionHandle.BottomLeft
        lockX: _private.pressedHandle && _private.pressedHandle.forwardDiagonal
        lockY: lockX
        drag.maximumX: handleTopRight.x - implicitWidth / 2
        drag.minimumY: handleTopRight.y + implicitHeight / 2
        Binding {
            target: _private; property: "pressedHandle"
            value: handleBottomLeft; when: !_private.applyingPendingRect && handleBottomLeft.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    SelectionHandle {
        id: handleBottom
        visible: !_private.isSquareRatio() && selectionArea.width >= implicitWidth
        target: selectionArea
        position: SelectionHandle.Bottom
        lockY: _private.pressedHandle && _private.pressedHandle.verticalOnly
        drag.minimumY: handleTop.y + implicitHeight / 2
        Binding {
            target: _private; property: "pressedHandle"
            value: handleBottom; when: handleBottom.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    SelectionHandle {
        id: handleBottomRight
        target: selectionArea
        position: SelectionHandle.BottomRight
        lockX: _private.pressedHandle && _private.pressedHandle.backwardDiagonal
        lockY: lockX
        drag.minimumX: handleTopLeft.x + implicitWidth / 2
        drag.minimumY: handleTopLeft.y + implicitHeight / 2
        Binding {
            target: _private; property: "pressedHandle"
            value: handleBottomRight; when: !_private.applyingPendingRect && handleBottomRight.pressed
            restoreMode: Binding.RestoreBindingOrValue
        }
    }
    // TODO: maybe scale proportions instead of just limiting size
    onWidthChanged: if (selectionArea.x + selectionArea.width > root.width) {
        selectionArea.width = Math.max(root.width - selectionArea.x, handleTopLeft.implicitWidth/2)
        if (selectionArea.x > root.width) {
            selectionArea.x = Math.max(root.width - selectionArea.width, 0)
        }
    }
    onHeightChanged: if (selectionArea.y + selectionArea.height > root.height) {
        selectionArea.height = Math.max(root.height - selectionArea.y, handleTopLeft.implicitHeight/2)
        if (selectionArea.y > root.height) {
            selectionArea.y = Math.max(root.height - selectionArea.height, 0)
        }
    }
}
