/* SPDX-FileCopyrightText: 2021 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
 */

import QtQuick
import org.kde.kirigami as Kirigami

RectangleCutout {
    id: root
    Rectangle {
        x: root.insideX - 1
        y: root.insideY - 1
        width: root.insideWidth + 2
        height: root.insideHeight + 2
        color: "transparent"
        border.color: Kirigami.Theme.highlightColor
        border.width: 1
    }
}
