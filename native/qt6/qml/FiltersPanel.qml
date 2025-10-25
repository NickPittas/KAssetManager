import QtQuick
import QtQuick.Controls
import KAssetManager 1.0
import "controls"

Item {
    id: root
    property AssetsModel assetsModel
    width: parent ? parent.width : 320
    implicitHeight: panel.implicitHeight

    // Tags data source
    TagsModel { id: tagsModel }

    Rectangle {
        id: panel
        width: parent.width
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border
        border.width: 1
        // Compute implicit height from content + margins so parents can reserve space
        implicitHeight: content.implicitHeight + 16
        // ensure existence by binding to count
        Component.onCompleted: console.debug("[TagsPanel] tags count:", tagsModel ? tagsModel.rowCount() : -1)
        Column {
            id: content
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8
            Row {
                spacing: 8
                Text { text: "Tags"; color: Theme.text; font.bold: true }
                ShButton { text: "+"; onClicked: newTagDialog.open() }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            // Tag filter mode toggle
            Row {
                spacing: 8
                Text { text: "Filter Mode:"; color: Theme.text }
                ShButton {
                    text: assetsModel.tagFilterMode === 0 ? "AND" : "OR"
                    onClicked: assetsModel.tagFilterMode = assetsModel.tagFilterMode === 0 ? 1 : 0
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            ListView {
                id: tagsList
                height: 160
                width: parent.width
                model: tagsModel
                delegate: Rectangle {
                    width: tagsList.width
                    height: 28
                    color: assetsModel.selectedTagNames.indexOf(name) !== -1 ? Theme.surfaceAlt : "transparent"
                    Row {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 8
                        Text { text: name; color: Theme.text }
                        ShButton { text: "Rename"; onClicked: tagsModel.renameTag(id, name + "*") }
                        ShButton { text: "Delete"; onClicked: tagsModel.deleteTag(id) }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            var currentTags = assetsModel.selectedTagNames
                            var index = currentTags.indexOf(name)
                            if (index === -1) {
                                currentTags.push(name)
                            } else {
                                currentTags.splice(index, 1)
                            }
                            assetsModel.selectedTagNames = currentTags
                        }
                    }
                    DropArea {
                        anchors.fill: parent
                        onDropped: { if (AppState.selectedAssetIds && AppState.selectedAssetIds.length>0) { assetsModel.assignTags(AppState.selectedAssetIds, [id]); LogManager.addLog("Assigned tag to " + AppState.selectedAssetIds.length + " asset(s)"); } }
                    }
                }
            }
        }
    }

    Dialog {
        id: newTagDialog
        title: "New Tag"
        modal: true
        width: Math.min(360, root.width)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            const name = tagNameField.text.trim();
            if (name.length > 0) tagsModel.createTag(name)
            tagNameField.text = ""
        }
        contentItem: Column {
            spacing: 8
            padding: 12
            TextField { id: tagNameField; placeholderText: "Tag name"; selectByMouse: true; focus: true }
        }
    }
}
