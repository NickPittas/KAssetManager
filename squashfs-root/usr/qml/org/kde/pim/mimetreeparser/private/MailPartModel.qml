// SPDX-FileCopyrightText: 2016 Michael Bohlender <michael.bohlender@kdemail.net>
// SPDX-FileCopyrightText: 2022 Carl Schwan <carl@carlschwan.eu>
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick 2.15
import QtQuick.Controls 2.15 as QQC2
import QtQuick.Layouts 1.15
import QtQml.Models 2.2
import org.kde.pim.mimetreeparser
import org.kde.kirigami 2.19 as Kirigami

DelegateModel {
    id: root

    property string searchString: ""
    property bool autoLoadImages: false
    property int padding: Kirigami.Units.largeSpacing
    property url icalCustomComponent
    required property var urlHandler

    delegate: RowLayout {
        id: partDelegate

        required property int type
        required property string content
        required property bool isEmbedded
        required property int sidebarSecurityLevel

        required property int encryptionSecurityLevel
        required property string encryptionIconName
        required property var encryptionDetails

        required property int signatureSecurityLevel
        required property string signatureIconName
        required property string signatureDetails

        required property int errorType
        required property string errorString

        readonly property bool isEncrypted: encryptionSecurityLevel !== PartModel.Unknow
        readonly property bool isSigned: signatureSecurityLevel !== PartModel.Unknow

        width: ListView.view.width
        spacing: Kirigami.Units.smallSpacing

        function getType(securityLevel: int): int {
            if (securityLevel === PartModel.Good) {
                return Kirigami.MessageType.Positive
            }
            if (securityLevel === PartModel.Bad) {
                return Kirigami.MessageType.Error
            }
            if (securityLevel === PartModel.NotSoGood) {
                return Kirigami.MessageType.Warning
            }
            return Kirigami.MessageType.Information
        }

        function getColor(securityLevel: int): color {
            if (securityLevel === PartModel.Good) {
                return Kirigami.Theme.positiveTextColor
            }
            if (securityLevel === PartModel.Bad) {
                return Kirigami.Theme.negativeTextColor
            }
            if (securityLevel === PartModel.NotSoGood) {
                return Kirigami.Theme.neutralTextColor
            }
            return "transparent"
        }

        QQC2.Control {
            id: sidebar

            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.preferredWidth: Kirigami.Units.smallSpacing
            Layout.fillHeight: true

            visible: partDelegate.sidebarSecurityLevel !== PartModel.Unknown

            background: Rectangle {
                id: border
                color: getColor(partDelegate.sidebarSecurityLevel)
            }
        }

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Layout.leftMargin: Kirigami.Units.gridUnit - (sidebar.visible ? Kirigami.Units.largeSpacing : 0) - Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.gridUnit

            Banner {
                iconName: partDelegate.encryptionIconName
                type: getType(partDelegate.encryptionSecurityLevel)
                visible: partDelegate.isEncrypted
                text: !partDelegate.encryptionDetails.keyId ? i18n("This message is encrypted but you don't have a matching secret key.") : i18n("This message is encrypted for: %1", partDelegate.encryptionDetails.keyId);

                Layout.fillWidth: true
            }

            Banner {
                iconName: partDelegate.signatureIconName
                visible: partDelegate.isSigned
                type: getType(partDelegate.signatureSecurityLevel)
                text: partDelegate.signatureDetails

                onLinkActivated: (link) => root.urlHandler.handleClick(link, QQC2.ApplicationWindow.window)

                Layout.fillWidth: true
            }

            Loader {
                id: partLoader

                Layout.preferredHeight: item ? item.contentHeight : 0
                Layout.fillWidth: true

                Component.onCompleted: {
                    switch (partDelegate.type) {
                        case PartModel.Plain:
                            partLoader.setSource("TextPart.qml", {
                                content: partDelegate.content,
                                embedded: partDelegate.isEmbedded,
                            })
                            break
                        case PartModel.Html:
                            partLoader.setSource("HtmlPart.qml", {
                                content: partDelegate.content,
                            })
                            break;
                        case PartModel.Error:
                            partLoader.setSource("ErrorPart.qml", {
                                errorType: partDelegate.errorType,
                                errorString: partDelegate.errorString,
                            })
                            break;
                        case PartModel.Encapsulated:
                            partLoader.setSource("MailPart.qml", {
                                rootIndex: root.modelIndex(index),
                                model: root.model,
                                sender: model.sender,
                                date: model.date,
                            })
                            break;
                        case PartModel.Ical:
                            partLoader.setSource(root.icalCustomComponent ? root.icalCustomComponent : "ICalPart.qml", {
                                content: partDelegate.content,
                            })
                            break;
                    }
                }

                Binding {
                    target: partLoader.item
                    property: "searchString"
                    value: root.searchString
                    when: partLoader.status === Loader.Ready
                }
                Binding {
                    target: partLoader.item
                    property: "autoLoadImages"
                    value: root.autoLoadImages
                    when: partLoader.status === Loader.Ready
                }
            }
        }
    }
}
