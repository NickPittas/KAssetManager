// SPDX-FileCopyrightText: 2021 Carl Schwan <carlschwan@kde.org>
// SPDX-FileCopyrightText: 2016 Michael Bohlender <michael.bohlender@kdemail.net>
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.kde.kirigami 2.14 as Kirigami
import QtQuick.Controls 2.15 as QQC2

QQC2.AbstractButton {
    id: root

    required property string name
    required property string type
    property alias actionIcon: actionButton.icon.name
    property alias actionTooltip: actionButton.text

    signal execute;
    signal publicKeyImport;

    Kirigami.Theme.colorSet: Kirigami.Theme.Button
    Kirigami.Theme.inherit: false

    background: Rectangle {
        id: background
        color: Kirigami.Theme.backgroundColor
        border.color: Kirigami.Theme.disabledTextColor
        radius: 3
    }

    leftPadding: Kirigami.Units.smallSpacing
    rightPadding: Kirigami.Units.smallSpacing
    topPadding: Kirigami.Units.smallSpacing
    bottomPadding: Kirigami.Units.smallSpacing
    contentItem: RowLayout {
        id: content
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            color: Kirigami.Theme.backgroundColor
            Layout.preferredHeight: Kirigami.Units.gridUnit
            Layout.preferredWidth: Kirigami.Units.gridUnit
            Kirigami.Icon {
                anchors.verticalCenter: parent.verticalCenter
                height: Kirigami.Units.gridUnit
                width: Kirigami.Units.gridUnit
                source: root.icon.name
            }
        }
        QQC2.Label {
            text: root.name
        }
        QQC2.ToolButton {
            visible: root.type === "application/pgp-keys"
            icon.name: 'gpg'
            text: i18ndc("mimetreeparser", "@action:button", "Import certificate")
            display: QQC2.ToolButton.IconOnly
            onClicked: root.publicKeyImport()

            QQC2.ToolTip.text: text
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.visible: hovered
        }
        QQC2.ToolButton {
            id: actionButton

            onClicked: root.execute()

            QQC2.ToolTip.text: text
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            QQC2.ToolTip.visible: hovered
        }
    }
}
