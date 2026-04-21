// SPDX-FileCopyrightText: 2016 Michael Bohlender <michael.bohlender@kdemail.net>
// SPDX-FileCopyrightText: 2017 Christian Mollekopf, <mollekopf@kolabsys.com>
// SPDX-FileCopyrightText: 2022 Carl Schwan <carl@carlschwan.eu>
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick 2.4
import QtQuick.Controls 2.15 as QQC2

Item {
    id: root
    property var errorType
    property string errorString
    property string searchString
    property bool autoLoadImages: false
    height: partListView.height
    width: parent.width

    Column {
        id: partListView
        anchors {
            top: parent.top
            left: parent.left
        }
        width: parent.width
        spacing: 5
        QQC2.Label {
            text: i18nd("mimetreeparser", "An error occurred: %1", errorString)
        }
    }
}
