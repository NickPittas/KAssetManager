pragma Singleton
import QtQml

QtObject {
    // Global UI/app state exposed as a typed QML singleton to silence AOT warnings
    property bool infoOpen: false
    property bool dragActive: false
    property string lastDropMessage: ""

    property int selectedFolderId: 0
    property int selectedAssetId: -1
    property var selectedAssetIds: []
    property int selectionAnchorIndex: -1
    property string selectedFileName: ""
    property string selectedFilePath: ""
    property int selectedFileSize: 0
    property string selectedFileType: ""
    property string selectedFileModified: ""
    property bool previewOpen: false
    property int previewIndex: -1
    property string previewSource: ""
    property string previewMediaType: ""

    property int newFolderParentId: 0
    property string newFolderName: ""
    property int renameFolderId: 0
    property string renameFolderName: ""
    property int deleteFolderId: 0
}
