import QtQuick
import QtQuick.Controls

Button {
    id: control
    implicitHeight: 32
    implicitWidth: Math.max(88, contentItem.implicitWidth + 20)
    hoverEnabled: true
    font.pixelSize: 13
    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.accent : (control.hovered ? Theme.accentBg : Theme.surface)
        border.color: control.checked ? Theme.accent : Theme.border
        border.width: 1
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
    contentItem: Text {
        text: control.text
        color: control.down || control.hovered ? Theme.text : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}