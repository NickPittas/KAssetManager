// SPDX-FileCopyrightText: 2022 Carl Schwan <carl@carlschwan.eu>
// SPDX-FileCopyrightText: 2022 Devin Lin <espidev@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15 as QQC2
import Qt.labs.platform 1.1 as QLP

import org.kde.pim.mimetreeparser 1.0
import org.kde.kirigami 2.20 as Kirigami
import './private'

Kirigami.ScrollablePage {
    id: root

    property alias message: mailPartView.message

    readonly property string subject: mailPartView.subject
    readonly property string from: mailPartView.from
    readonly property string sender: mailPartView.sender
    readonly property string to: mailPartView.to
    readonly property string cc: mailPartView.cc
    readonly property string bcc: mailPartView.bcc
    readonly property date dateTime: mailPartView.dateTime

    /**
     * This property holds the url to a custom ical part QML component.
     *
     * This allow apps to overwrite it and provide special handling for email
     * invitation.
     */
    property url icalCustomComponent

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    padding: Kirigami.Units.largeSpacing

    title: i18ndc("mimetreeparser", "@title:window", "Message viewer")

    header: QQC2.ToolBar {
        id: mailHeader

        padding: root.padding
        visible: root.from.length > 0 || root.to.length > 0 || root.subject.length > 0

        Kirigami.Theme.inherit: false
        Kirigami.Theme.colorSet: Kirigami.Theme.View

        background: Item {
            Rectangle {
                anchors.fill: parent
                color: Kirigami.Theme.alternateBackgroundColor
            }

            Kirigami.Separator {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
            }

            Kirigami.Separator {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
            }
        }

        contentItem: GridLayout {
            rowSpacing: Kirigami.Units.smallSpacing
            columnSpacing: Kirigami.Units.smallSpacing

            columns: 3

            QQC2.Label {
                text: i18ndc("mimetreeparser", "@label", "Subject:")
                visible: root.subject.length > 0

                Layout.rightMargin: Kirigami.Units.largeSpacing
            }

            QQC2.Label {
                text: root.subject
                visible: text.length > 0
                elide: Text.ElideRight

                Layout.fillWidth: true
            }

            QQC2.Label {
                text: root.dateTime.toLocaleString(Qt.locale(), Locale.ShortFormat)
                visible: text.length > 0
                horizontalAlignment: Text.AlignRight
            }

            QQC2.Label {
                text: i18ndc("mimetreeparser", "@label", "From:")
                visible: root.from.length > 0

                Layout.rightMargin: Kirigami.Units.largeSpacing
            }

            QQC2.Label {
                text: root.from
                visible: text.length > 0
                elide: Text.ElideRight

                Layout.fillWidth: true
                Layout.columnSpan: 2
            }

            QQC2.Label {
                text: i18ndc("mimetreeparser", "@label", "Sender:")
                visible: root.sender.length > 0 && root.sender !== root.from

                Layout.rightMargin: Kirigami.Units.largeSpacing
            }

            QQC2.Label {
                visible: root.sender.length > 0 && root.sender !== root.from
                text: root.sender
                elide: Text.ElideRight

                Layout.fillWidth: true
                Layout.columnSpan: 2
            }

            QQC2.Label {
                text: i18ndc("mimetreeparser", "@label", "To:")
                visible: root.to.length > 0

                Layout.rightMargin: Kirigami.Units.largeSpacing
            }

            QQC2.Label {
                text: root.to
                elide: Text.ElideRight
                visible: root.to.length > 0

                Layout.fillWidth: true
                Layout.columnSpan: 2
            }

            QQC2.Label {
                text: i18ndc("mimetreeparser", "@label", "CC:")
                visible: root.cc.length > 0

                Layout.rightMargin: Kirigami.Units.largeSpacing
            }

            QQC2.Label {
                text: root.cc
                elide: Text.ElideRight
                visible: root.cc.length > 0

                Layout.fillWidth: true
                Layout.columnSpan: 2
            }

            QQC2.Label {
                text: i18ndc("mimetreeparser", "@label", "BCC:")
                visible: root.bcc.length > 0

                Layout.rightMargin: Kirigami.Units.largeSpacing
            }

            QQC2.Label {
                text: root.bcc
                elide: Text.ElideRight
                visible: root.bcc.length > 0

                Layout.fillWidth: true
                Layout.columnSpan: 2
            }
        }
    }

    MailPartView {
        id: mailPartView
        padding: root.padding
        icalCustomComponent: root.icalCustomComponent
    }

    footer: QQC2.ToolBar {
        padding: root.padding

        Kirigami.Theme.inherit: false
        Kirigami.Theme.colorSet: Kirigami.Theme.View

        visible: attachmentRepeater.count > 0

        background: Item {
            Kirigami.Separator {
                anchors {
                    left: parent.left
                    right: parent.right
                    top: undefined
                    bottom:  parent.bottom
                }
            }
        }

        Flow {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing
            Repeater {
                id: attachmentRepeater

                model: mailPartView.attachmentModel

                delegate: AttachmentDelegate {
                    id: attachmentDelegate

                    required property int index
                    required property string iconName

                    icon.name: iconName

                    clip: true

                    actionIcon: 'download'
                    actionTooltip: i18ndc("mimetreeparser", "@action:button", "Save attachment")
                    onClicked: mailPartView.attachmentModel.openAttachment(index)
                    onPublicKeyImport: mailPartView.attachmentModel.importPublicKey(index)
                    onExecute: {
                        const dialog = saveFileDialog.createObject(applicationWindow(), {
                            fileName: name,
                        });
                        dialog.onAccepted.connect(() => {
                            mailPartView.attachmentModel.saveAttachmentToPath(0, dialog.file.toString().slice(7))
                        });
                        dialog.open();
                    }
                }
            }
        }
    }

    Component {
        id: saveFileDialog

        QLP.FileDialog {
            required property string fileName

            title: i18ndc("mimetreeparser", "@window:title", "Save Attachment As")
            currentFile: QLP.StandardPaths.writableLocation(QLP.StandardPaths.DownloadLocation) + '/' + fileName
            folder: QLP.StandardPaths.writableLocation(QLP.StandardPaths.DownloadLocation)
            fileMode: QLP.FileDialog.SaveFile
        }
    }

    Connections {
        target: mailPartView.attachmentModel

        function onInfo(message) {
            applicationWindow().showPassiveNotification(message);
        }

        function onErrorOccurred(message) {
            applicationWindow().showPassiveNotification(message);
        }
    }
}
