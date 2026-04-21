// SPDX-FileCopyrightText: 2021 Carl Schwan <carlschwan@kde.org>
// SPDX-FileCopyrightText: 2016 Michael Bohlender <michael.bohlender@kdemail.net>
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.pim.mimetreeparser

ListView {
    id: root

    property alias message: messageParser.message
    readonly property string subject: messageParser.subject
    readonly property string from: messageParser.from
    readonly property string sender: messageParser.sender
    readonly property string to: messageParser.to
    readonly property string cc: messageParser.cc
    readonly property string bcc: messageParser.bcc
    readonly property date dateTime: messageParser.date

    property alias rootIndex: visualModel.rootIndex
    property alias padding: visualModel.padding
    property alias searchString: visualModel.searchString
    property alias autoLoadImages: visualModel.autoLoadImages
    property var attachmentModel: messageParser.attachments

    property url icalCustomComponent

    topMargin: padding
    bottomMargin: padding

    spacing: Kirigami.Units.smallSpacing

    model: MailPartModel {
        id: visualModel
        model: messageParser.parts
        icalCustomComponent: root.icalCustomComponent
        urlHandler: urlHandler
    }

    MessageParser {
        id: messageParser
    }

    MimeUrlHandler {
        id: urlHandler

        onErrorOccurred: (errorMessage) => QQC2.ApplicationWindow.window.showPassiveNotification(errorMessage)
    }
}
