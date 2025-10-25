import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import QtCore
import QtQuick.Dialogs
import KAssetManager 1.0
import "controls"


ApplicationWindow {
    id: root
    visible: true
    width: 1200; height: 800
    title: "KAsset Manager (Qt6)"
    color: Theme.bg
    background: Rectangle { color: Theme.bg }
    property int debugProgressStep: 0
    property bool progressActive: false
    property string progressMessage: ""
    property int progressCurrent: 0
    property int progressTotal: 0
    property int progressPercent: 0
    property string viewMode: appSettings.viewMode
    property bool diagnosticsEnabled: {
        try {
            return Qt.application && Qt.application.arguments && Qt.application.arguments.indexOf("--diag") !== -1
        } catch (err) {
            return false
        }
    }
    property bool diagnosticsVerboseEnabled: {
        try {
            return Qt.application && Qt.application.arguments && Qt.application.arguments.indexOf("--verbose") !== -1
        } catch (err) {
            return false
        }
    }
    property bool autotestEnabled: {
        try {
            if (!diagnosticsEnabled || !Qt.application || !Qt.application.arguments)
                return false
            return Qt.application.arguments.indexOf("--autotest") !== -1
        } catch (err) {
            return false
        }
    }
    property string autotestMediaPath: ""
    property int autotestBatchCount: 0
    property bool autotestRunning: false
    property bool autotestCompleted: false
    property bool autotestImportSucceeded: false
    property int autotestProgressActivationCount: 0
    property int autotestProgressDeactivationCount: 0
    property int autotestThumbnailCount: 0

    Settings {
        id: appSettings
        property string viewMode: "grid"
    }

    function diagLog() {
        if (!diagnosticsEnabled) return
        if (diagnosticsVerboseEnabled) {
            console.log.apply(console, arguments)
        } else {
            try { LogManager.addLog(Array.prototype.join.call(arguments, ' '), 'DEBUG') } catch (e) {}
        }
    }

    function diagWarn() {
        if (!diagnosticsEnabled) return
        if (diagnosticsVerboseEnabled) {
            console.warn.apply(console, arguments)
        } else {
            try { LogManager.addLog(Array.prototype.join.call(arguments, ' '), 'WARN') } catch (e) {}
        }
    }

    function escapeRegex(str) {
        return str.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
    }


    function humanDateTime(dt) {
        if (!dt) return "";
        try { return Qt.formatDateTime(dt, Qt.DefaultLocaleShortDate); } catch (err) { return ""; }
    }
    function htmlEscape(text) {
        if (text === null || text === undefined)
            return ""
        if (typeof Qt !== "undefined" && typeof Qt.htmlEscaped === "function")
            return Qt.htmlEscaped(text)
        return String(text).replace(/[&<>"']/g, function(ch) {
            switch (ch) {
            case "&": return "&amp;"
            case "<": return "&lt;"
            case ">": return "&gt;"
            case "\"": return "&quot;"
            case "'": return "&#39;"
            default: return ch
            }
        })
    }

    function highlightMatch(text, baseColor) {
        if (text === null || text === undefined)
            text = ""
        else
            text = String(text)
        const query = assetsModel.searchQuery || ""
        const rootColor = baseColor || "#ddd"
        if (!query)
            return "<span style='color:" + rootColor + ";'>" + htmlEscape(text) + "</span>"

        const regex = new RegExp(escapeRegex(query), "ig")
        let result = ""
        let lastIndex = 0
        let match
        while ((match = regex.exec(text)) !== null) {
            const start = match.index
            const end = start + match[0].length
            result += htmlEscape(text.slice(lastIndex, start))
            result += "<span style='color:#4a90e2;font-weight:bold;'>" + htmlEscape(match[0]) + "</span>"
            lastIndex = end
            if (match[0].length === 0)
                regex.lastIndex++
        }
        result += htmlEscape(text.slice(lastIndex))
        return "<span style='color:" + rootColor + ";'>" + result + "</span>"
    }

    function toFileUrl(path) {
        if (!path || path.length === 0)
            return ""
        if (path.startsWith("file:"))
            return path
        var url = path.replace(/\\/g, "/")
        if (url.length > 0 && url[0] !== "/")
            url = "/" + url
        return "file://" + url
    }

    function setViewMode(mode) {
        if (viewMode === mode)
            return
        viewMode = mode
        appSettings.viewMode = mode
        LogManager.addLog("View mode changed to " + mode)
    }

    function selectRange(anchorIndex, toIndex) {
        if (!assetsModel) return []
        var lo = Math.min(anchorIndex, toIndex)
        var hi = Math.max(anchorIndex, toIndex)
        var ids = []
        for (var i = lo; i <= hi; ++i) {
            var item = assetsModel.get(i)
            if (item && item.assetId !== undefined) ids.push(item.assetId)
        }
        return ids
    }


    function currentAssetIndex() {
        var candidate = viewMode === "list" ? listView.currentIndex : filesGrid.currentIndex
        return candidate === undefined ? -1 : candidate
    }

    function openPreview(index) {
        if (index === undefined || index === null)
            index = currentAssetIndex()
        if (!assetsModel || index < 0 || index >= assetsModel.rowCount())
            return
        var item = assetsModel.get(index)
        if (!item)
            return
        AppState.previewIndex = index
        AppState.previewSource = item.filePath
        AppState.previewMediaType = ThumbnailGenerator.isVideoFile(item.filePath) ? "video" : "image"
        AppState.previewOpen = true
        LogManager.addLog("Preview opened: " + (item.fileName || item.filePath))
        Qt.callLater(function() { if (previewOverlay.visible) previewOverlay.forceActiveFocus(); })
    }

    function closePreview() {
        if (!AppState.previewOpen)
            return
        AppState.previewOpen = false
        AppState.previewSource = ""
        AppState.previewMediaType = ""
        AppState.previewIndex = -1
        LogManager.addLog("Preview closed")
    }

    function changePreview(step) {
        if (!AppState.previewOpen)
            return
        var next = AppState.previewIndex + step
        if (next < 0 || !assetsModel || next >= assetsModel.rowCount())
            return
        openPreview(next)
    }

    function parseArguments() {
        if (!Qt.application || !Qt.application.arguments)
            return
        for (var i = 0; i < Qt.application.arguments.length; ++i) {
            var arg = Qt.application.arguments[i]
            if (arg.indexOf("--autotest-media=") === 0) {
                autotestMediaPath = arg.substring("--autotest-media=".length)
            } else if (arg.indexOf("--autotest-batch=") === 0) {
                var v = parseInt(arg.substring("--autotest-batch=".length))
                autotestBatchCount = isNaN(v) ? 0 : Math.max(0, v)
            }
        }
    }

    Component.onCompleted: {
        parseArguments()
        root.progressActive = ProgressManager.isActive
        root.progressMessage = ProgressManager.message
        root.progressCurrent = ProgressManager.current
        root.progressTotal = ProgressManager.total
        root.progressPercent = ProgressManager.percentage
        LogManager.addLog("UI initialized - view mode " + viewMode)
        if (autotestEnabled) {
            if (!autotestMediaPath || autotestMediaPath.length === 0) {
                autotestMediaPath = ThumbnailGenerator.createSampleImage("")
                if (autotestMediaPath && autotestMediaPath.length > 0) {
                    diagLog("[autotest] Generated sample image:", autotestMediaPath)
                } else {
                    diagWarn("[autotest] Failed to generate sample image")
                }
            }
            Qt.callLater(startAutotest)
        }
    }


    // Typed QML types registered from C++ (no context properties)
    VirtualFolderTreeModel {
        id: folderModel
        Component.onCompleted: {
            diagLog("FolderModel loaded, rootId:", rootId())
            LogManager.addLog("Folders ready (root id: " + rootId() + ")")
            // Initialize assetsModel with root folder
            assetsModel.folderId = rootId()
            AppState.selectedFolderId = rootId()
        }
    }
    AssetsModel {
        id: assetsModel
        onFolderIdChanged: {
            diagLog("AssetsModel folderId changed to:", folderId, "rowCount:", rowCount())
            LogManager.addLog("Loaded folder " + folderId + " (" + rowCount() + " assets)")
        }
    }
Importer {
        id: importer
        onImportCompleted: function(count) {
            diagLog("Import completed:", count, "files")
            LogManager.addLog("Import completed with " + count + " item(s)")
            AppState.previewIndex = assetsModel.rowCount() > 0 ? 0 : -1
            // Let DB signals drive a debounced reload; avoid immediate re-entrancy
            // If the current view is empty (e.g., imported into a different folder), request a single explicit reload
            if (assetsModel.rowCount() === 0) {
                assetsModel.reload()
            }
            if (autotestRunning) {
                if (count <= 0) {
                    finishAutotest(false, "Importer reported zero files")
                    return
                }
                autotestImportSucceeded = count > 0
                diagLog("[autotest] importCompleted count:", count)
                autotestEvalTimer.restart()
            }
        }
    }
    Connections {
        target: LogManager
        function onLogAdded(message) {
            if (diagnosticsVerboseEnabled) diagLog("LogManager.logAdded:", message)
            Qt.callLater(function() {
                if (typeof logView !== "undefined" && logView) {
                    logView.positionViewAtEnd()
                }
            })
        }
        function onLogsChanged() {
            if (diagnosticsVerboseEnabled) diagLog("LogManager.logs changed; size:", LogManager.logs.length)
            Qt.callLater(function() {
                if (typeof logView !== "undefined" && logView) {
                    logView.positionViewAtEnd()
                }
            })
        }
    }
    Connections {
        target: ProgressManager
        function onIsActiveChanged() {
            if (diagnosticsVerboseEnabled) diagLog("ProgressManager.isActive changed:", ProgressManager.isActive)
            root.progressActive = ProgressManager.isActive
            if (autotestRunning) {
                if (ProgressManager.isActive) {
                    autotestProgressActivationCount += 1
                } else {
                    autotestProgressDeactivationCount += 1
                    autotestEvalTimer.restart()
                }
            }
            LogManager.addLog(ProgressManager.isActive ? "Progress started" : "Progress finished")
        }
        function onMessageChanged() {
            if (diagnosticsVerboseEnabled) diagLog("ProgressManager.message changed:", ProgressManager.message)
            root.progressMessage = ProgressManager.message
            if (ProgressManager.message && ProgressManager.message.length > 0) {
                LogManager.addLog("Progress: " + ProgressManager.message)
            }
        }
        function onCurrentChanged() {
            if (diagnosticsVerboseEnabled) diagLog("ProgressManager.current changed:", ProgressManager.current)
            root.progressCurrent = ProgressManager.current
        }
        function onTotalChanged() {
            if (diagnosticsVerboseEnabled) diagLog("ProgressManager.total changed:", ProgressManager.total)
            root.progressTotal = ProgressManager.total
            LogManager.addLog("Progress total: " + ProgressManager.total)
        }
        function onPercentageChanged() {
            if (diagnosticsVerboseEnabled) diagLog("ProgressManager.percentage changed:", ProgressManager.percentage)
            root.progressPercent = ProgressManager.percentage
            if (ProgressManager.percentage > 0) {
                LogManager.addLog("Progress: " + ProgressManager.percentage + "%")
            }
        }
    }
    Connections {
        target: ThumbnailGenerator
        function onThumbnailGenerated(filePath, thumbnailPath) {
            if (diagnosticsVerboseEnabled) diagLog("ThumbnailGenerator.thumbnailGenerated:", filePath, "->", thumbnailPath)
            if (autotestRunning) {
                autotestThumbnailCount += 1
                autotestEvalTimer.restart()
            }
        }
        function onThumbnailFailed(filePath) {
            if (diagnosticsVerboseEnabled) diagWarn("ThumbnailGenerator.thumbnailFailed:", filePath)
            if (autotestRunning) {
                finishAutotest(false, "Thumbnail generation failed for " + filePath)
            }
        }
    }
    Connections {
        target: assetsModel
        function onDataChanged(topLeft, bottomRight, roles) {
            if (diagnosticsVerboseEnabled) diagLog("AssetsModel.dataChanged:", topLeft.row, "->", bottomRight.row, "roles:", roles)
            if (autotestRunning) {
                autotestEvalTimer.restart()
            }
        }
    }

    Timer {
        id: autotestEvalTimer
        interval: 500
        repeat: false
        onTriggered: evaluateAutotest()
    }

    Timer {
        id: autotestTimeoutTimer
        interval: 15000
        repeat: false
        onTriggered: finishAutotest(false, "Autotest timed out")
    }

    function startAutotest() {
        if (!autotestEnabled || autotestCompleted)
            return
        if (autotestRunning)
            return
        autotestRunning = true
        autotestImportSucceeded = false
        autotestProgressActivationCount = 0
        autotestProgressDeactivationCount = 0
        autotestThumbnailCount = 0
        ThumbnailGenerator.clearCache()

        var paths = []
        if (autotestBatchCount && autotestBatchCount > 1) {
            diagLog("[autotest] Generating batch of", autotestBatchCount, "images")
            for (var i = 0; i < autotestBatchCount; ++i) {
                var p = ThumbnailGenerator.createSampleImage("")
                if (p && p.length > 0)
                    paths.push(p)
            }
        } else {
            if (!autotestMediaPath || autotestMediaPath.length === 0) {
                autotestMediaPath = ThumbnailGenerator.createSampleImage("")
            }
            if (autotestMediaPath && autotestMediaPath.length > 0)
                paths = [autotestMediaPath]
        }

        if (!paths || paths.length === 0) {
            finishAutotest(false, "No media generated for autotest")
            return
        }

        diagLog("[autotest] Starting integration workflow with", paths.length, "file(s)")
        var ok = importer.importPaths(paths)
        if (!ok) {
            diagWarn("[autotest] importPaths returned false")
            finishAutotest(false, "importPaths returned false")
        } else {
            autotestTimeoutTimer.start()
        }
    }

    function evaluateAutotest() {
        if (!autotestRunning || autotestCompleted)
            return
        if (!autotestImportSucceeded) {
            diagLog("[autotest] Waiting for import completion")
            autotestEvalTimer.restart()
            return
        }
        if (ProgressManager.isActive) {
            diagLog("[autotest] Waiting for progress to finish")
            autotestEvalTimer.restart()
            return
        }
        if (autotestThumbnailCount < 1) {
            diagLog("[autotest] Awaiting thumbnail generation")
            autotestEvalTimer.restart()
            return
        }
        var rows = assetsModel.rowCount ? assetsModel.rowCount() : 0
        if (rows === 0) {
            diagLog("[autotest] Waiting for assetsModel to report rows")
            autotestEvalTimer.restart()
            return
        }
        var logCount = LogManager.logs ? LogManager.logs.length : 0
        if (logCount === 0) {
            diagLog("[autotest] Waiting for log entries")
            autotestEvalTimer.restart()
            return
        }
        var success = autotestImportSucceeded &&
                      autotestThumbnailCount > 0 &&
                      autotestProgressActivationCount > 0 &&
                      autotestProgressDeactivationCount > 0
        finishAutotest(success, success
            ? "Integration autotest completed"
            : "Autotest assertions failed (rows=" + rows + ", logs=" + logCount + ", thumbs=" + autotestThumbnailCount + ")")
    }

    function finishAutotest(success, message) {
        if (autotestCompleted)
            return
        autotestCompleted = true
        autotestRunning = false
        autotestTimeoutTimer.stop()
        autotestEvalTimer.stop()
        if (success) {
            diagLog("[autotest] PASS:", message)
            Qt.callLater(function() { Qt.exit(0) })
        } else {
            diagWarn("[autotest] FAIL:", message)
            Qt.callLater(function() { Qt.exit(1) })
        }
    }


    function humanSize(bytes) {
        var b = Number(bytes)
        if (isNaN(b) || b <= 0) return ""
        var units = ["B","KB","MB","GB","TB"]
        var i = 0
        while (b >= 1024 && i < units.length-1) { b /= 1024; i++ }
        return b.toFixed(i === 0 ? 0 : 1) + " " + units[i]
    }

    header: ToolBar {
        padding: 0
        implicitHeight: 72
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 0
        }
        Column {
            width: parent.width
            spacing: 4

            // Top toolbar
            Row {
                spacing: 8
                width: parent.width
                height: 40
                leftPadding: 8
                rightPadding: 8

                ShButton {
                    text: AppState.infoOpen ? "Info: On" : "Info"
                    onClicked: {
                        AppState.infoOpen = !AppState.infoOpen
                        LogManager.addLog(AppState.infoOpen ? "Info panel opened" : "Info panel closed")
                    }
                }
                ShButton {
                    text: "Import"
                    onClicked: importMenu.open()
                }
                Menu {
                    id: importMenu
                    MenuItem { text: "Files..."; onTriggered: importFilesDialog.open() }
                    MenuItem { text: "Folder..."; onTriggered: importFolderDialog.open() }
                }
                ShButton {
                    text: "Move to Folder"
                    enabled: AppState.selectedAssetId>0 && AppState.selectedFolderId>0
                    onClicked: {
                        LogManager.addLog("Move asset request for assetId " + AppState.selectedAssetId + " to folder " + AppState.selectedFolderId)
                        assetsModel.moveAssetToFolder(AppState.selectedAssetId, AppState.selectedFolderId)
                    }
                }

                FileDialog {
                    id: importFilesDialog
                    title: "Import Files"
                    fileMode: FileDialog.OpenFiles
                    nameFilters: ["Media files (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff *.ico *.svg *.mp4 *.mov *.avi *.mkv *.webm *.flv *.wmv *.m4v *.mpg *.mpeg)", "All files (*)"]
                    onAccepted: {
                        var paths = []
                        for (var i = 0; i < selectedFiles.length; ++i) {
                            var s = selectedFiles[i]
                            var p = (typeof s === 'string') ? s.replace(/^file:\/\//, '') : s.toString().replace(/^file:\/\//, '')
                            if (p && p.length>0) paths.push(p)
                        }
                        if (paths.length>0) {
                            LogManager.addLog("Import requested for " + paths.length + " file(s)")
                            importer.importPaths(paths)
                        }
                    }
                }
                FolderDialog {
                    id: importFolderDialog
                    title: "Import Folder"
                    onAccepted: {
                        if (folder && folder.toString) {
                            var p = folder.toString().replace(/^file:\/\//, '')
                            if (p && p.length>0) {
                                LogManager.addLog("Import requested for folder: " + p)
                                importer.importFolder(p, 0)
                            }
                        }
                    }
                }
            }

            // Tab bar
            TabBar {
                id: tabBar
                width: parent.width
                background: Rectangle {
                    color: Theme.surface
                    border.color: Theme.border
                    border.width: 1
                }

TabButton {
    text: "Browser"
    implicitHeight: 32
    implicitWidth: 120
    background: Rectangle {
        radius: 6
        color: parent.checked ? Theme.accentBg : Theme.surface
        border.color: parent.checked ? Theme.accent : Theme.border
    }
    contentItem: Text {
        text: parent.text
        color: parent.checked ? Theme.text : Theme.textMuted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        anchors.fill: parent
        font.bold: parent.checked
    }
}
TabButton {
    text: "Log"
    implicitHeight: 32
    implicitWidth: 120
    background: Rectangle {
        radius: 6
        color: parent.checked ? Theme.accentBg : Theme.surface
        border.color: parent.checked ? Theme.accent : Theme.border
    }
    contentItem: Text {
        text: parent.text
        color: parent.checked ? Theme.text : Theme.textMuted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        anchors.fill: parent
        font.bold: parent.checked
    }
}
TabButton {
    text: "Settings"
    implicitHeight: 32
    implicitWidth: 120
    background: Rectangle {
        radius: 6
        color: parent.checked ? Theme.accentBg : Theme.surface
        border.color: parent.checked ? Theme.accent : Theme.border
    }
    contentItem: Text {
        text: parent.text
        color: parent.checked ? Theme.text : Theme.textMuted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        anchors.fill: parent
        font.bold: parent.checked
    }
}
            }
        }
    }



    // focus intentionally omitted; Window has no 'focus' property in Qt6


    // Drag-in from Explorer (files/folders)
    DropArea {
        id: dropper
        anchors.fill: parent
        keys: [ "text/uri-list" ]
        onEntered: function(drag) {
            if (drag.hasUrls) {
                drag.acceptProposedAction();
                AppState.dragActive = true;
            }
        }
        onExited: AppState.dragActive = false
        onDropped: function(drop) {
            AppState.dragActive = false
            if (drop.hasUrls) {
                var files = []
                for (var i = 0; i < drop.urls.length; ++i) {
                    var url = drop.urls[i]
                    var f = (typeof url === 'string') ? url.replace(/^file:\/\/\//, '') : url.toString().replace(/^file:\/\/\//, '')
                    if (f && f.length > 0) files.push(f)
                }
                console.log("Importing files:", files)
                LogManager.addLog("Import requested for " + files.length + " item(s)")
                const ok = importer.importPaths(files)
                AppState.lastDropMessage = ok ? `Imported ${files.length} item(s)` : "Drop failed"
                console.log("Import result:", ok)
                LogManager.addLog(ok ? "Import succeeded for " + files.length + " item(s)" : "Import failed")
            }
        }
    }

    // Visual overlay when dragging files over the window
    Rectangle {
        anchors.fill: parent
        visible: AppState.dragActive
        color: "#66FFFFFF"
        border.color: "#33FFFFFF"
        border.width: 2
        z: 10
        Text {
            anchors.centerIn: parent
            text: "Drop files to import"
            color: "#000"
            font.pixelSize: 24
        }
    }

    // Main content area with tabs
    StackLayout {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.bottomMargin: root.progressActive ? 60 : 16
        currentIndex: tabBar.currentIndex

        // Tab 0: Browser
        Item {
            Column {
                anchors.fill: parent
                spacing: 12

                // Virtual browser: left = virtual folders, right = assets in selected folder
                Row {
            id: browserRow
            width: parent.width
            height: parent.height - 16 // Fill available height minus margins
            spacing: 12

            // Left: Virtual Folders tree
            Rectangle {
                id: foldersPane
                width: 320; height: parent.height
                color: Theme.surfaceAlt; radius: Theme.radius; border.color: Theme.border; border.width: 1
                Column {
                    anchors.fill: parent; anchors.margins: 8; spacing: 6
                    Label { text: "Folders"; color: "#EEE"; font.bold: true }
                    TreeView {
                        id: folderTree
                        width: parent.width
                        height: Math.max(120, parent.height - tagsPanel.implicitHeight - 30)
                        clip: true
                        model: folderModel
                        delegate: Rectangle {
                            id: treeDelegate
                            implicitHeight: 28
                            implicitWidth: folderTree.width
                            color: AppState.selectedFolderId === model.id ? "#2a5a8a" : "transparent"

                            required property int id
                            required property string name
                            required property int depth
                            required property bool hasChildren
                            required property bool isTreeNode
                            required property bool expanded

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 4
                                spacing: 4

                                Item { width: treeDelegate.depth * 16; height: 1 }

                                Text {
                                    text: treeDelegate.hasChildren ? (treeDelegate.expanded ? "â–¼" : "â–¶") : " "
                                    color: "#888"
                                    font.pixelSize: 10
                                    verticalAlignment: Text.AlignVCenter
                                    width: 16
                                }

                                Text {
                                    text: treeDelegate.name
                                    color: AppState.selectedFolderId === treeDelegate.id ? "#fff" : "#ddd"
                                    verticalAlignment: Text.AlignVCenter
                                    font.bold: AppState.selectedFolderId === treeDelegate.id
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: function(mouse) {
                                    console.log("Folder clicked:", treeDelegate.name, "id:", treeDelegate.id)
                                    if (mouse.button === Qt.RightButton) {
                                        AppState.selectedFolderId = treeDelegate.id
                                        folderMenu.popup()
                                    } else {
                                        // Toggle expand/collapse if has children
                                        if (treeDelegate.hasChildren) {
                                            folderTree.toggleExpanded(row)
                                        }
                                        // Select folder and load assets
                                        AppState.selectedFolderId = treeDelegate.id
                                        assetsModel.folderId = treeDelegate.id
                                        console.log("Set assetsModel.folderId to:", treeDelegate.id)
                                        LogManager.addLog("Folder selected: " + treeDelegate.name)
                                    }
                                }
                            }
DropArea {
                                anchors.fill: parent
                                onDropped: { if (AppState.selectedAssetIds && AppState.selectedAssetIds.length>0) { assetsModel.moveAssetsToFolder(AppState.selectedAssetIds, treeDelegate.id); LogManager.addLog("Moved " + AppState.selectedAssetIds.length + " asset(s) to folder id " + treeDelegate.id); } }
                            }

                            Menu {
                                id: folderMenu
                                background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.border }
                                MenuItem { text: "New Folder..."; onTriggered: { AppState.newFolderParentId = treeDelegate.id; AppState.newFolderName=""; newFolderDialog.open() } }
                                MenuItem { text: "Rename..."; onTriggered: { AppState.renameFolderId = treeDelegate.id; AppState.renameFolderName = treeDelegate.name; renameFolderDialog.open() } }
                                MenuItem { text: "Delete..."; onTriggered: { AppState.deleteFolderId = treeDelegate.id; deleteConfirmDialog.open() } }
                            }
                        }
                    }
                    // Tags panel below folders
                    FiltersPanel { id: tagsPanel; assetsModel: assetsModel }
                }
            }

            // Right: Assets grid
            Rectangle {
                id: filesPane
                width: parent.width - foldersPane.width - infoPane.width - 12; height: parent.height
                color: Theme.surfaceAlt; radius: Theme.radius; border.color: Theme.border; border.width: 1
                clip: true // Clip content to prevent overflow
                Column {
                    anchors.fill: parent; anchors.margins: 8; spacing: 6
                    Row {
                        id: viewToggleRow
                        spacing: 8
                        width: parent.width
                        Label { text: "Assets"; color: "#EEE"; font.bold: true; Layout.alignment: Qt.AlignVCenter }
                        Item { width: 12; height: 1 }
                        ButtonGroup { id: viewModeGroup }
                        ShButton {
                            text: "Grid"
                            checkable: true
                            ButtonGroup.group: viewModeGroup
                            checked: viewMode === "grid"
                            onClicked: setViewMode("grid")
                        }
                        ShButton {
                            text: "List"
                            checkable: true
                            ButtonGroup.group: viewModeGroup
                            checked: viewMode === "list"
                            onClicked: setViewMode("list")
                        }
                        Item { width: 12; height: 1 }
                        // Type filter at top of assets pane
                        ButtonGroup { id: typeFilterGroup }
                        ShButton { text: "All"; checkable: true; ButtonGroup.group: typeFilterGroup; checked: assetsModel && assetsModel.typeFilter===0; onClicked: assetsModel.typeFilter=0 }
                        ShButton { text: "Images"; checkable: true; ButtonGroup.group: typeFilterGroup; checked: assetsModel && assetsModel.typeFilter===1; onClicked: assetsModel.typeFilter=1 }
                        ShButton { text: "Videos"; checkable: true; ButtonGroup.group: typeFilterGroup; checked: assetsModel && assetsModel.typeFilter===2; onClicked: assetsModel.typeFilter=2 }
                    }
                    TextField {
                        id: assetSearchField
                        width: parent.width
                        placeholderText: "Search assets..."
                        text: assetsModel.searchQuery
                        onTextChanged: assetsModel.searchQuery = text
                        onEditingFinished: {
                            LogManager.addLog(text && text.length > 0 ? `Search applied: "${text}"` : "Search cleared")
                        }
                        selectByMouse: true
                        inputMethodHints: Qt.ImhNoPredictiveText
                        color: "#eee"
                        placeholderTextColor: "#666"
                        background: Rectangle {
                            implicitHeight: 32
                            radius: Theme.radius
                            color: Theme.surface
                            border.color: Theme.border
                        }
                    }
                        // Global right-click context menu for selection
                        Menu {
                            id: bulkMenu
                            background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.border }
                            MenuItem { text: "Move to selected folder"; enabled: AppState.selectedFolderId>0; onTriggered: assetsModel.moveAssetsToFolder(AppState.selectedAssetIds, AppState.selectedFolderId) }
                            MenuSeparator{}
                            MenuItem { text: "Set Rating → 0"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds, 0) }
                            MenuItem { text: "Set Rating → 1"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds, 1) }
                            MenuItem { text: "Set Rating → 2"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds, 2) }
                            MenuItem { text: "Set Rating → 3"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds, 3) }
                            MenuItem { text: "Set Rating → 4"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds, 4) }
                            MenuItem { text: "Set Rating → 5"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds, 5) }
                            MenuSeparator{}
                            MenuItem { text: "Remove from App"; onTriggered: assetsModel.removeAssets(AppState.selectedAssetIds) }
                        }
                        // Right-click handler that does not block item menus
                        GridView {
                            id: filesGrid
                            visible: viewMode === "grid"
                            width: parent.width
                            height: parent.height - assetSearchField.height - viewToggleRow.height - 30
                            cellWidth: 160; cellHeight: 160
                            model: assetsModel
                            focus: viewMode === "grid"
                            keyNavigationWraps: true
                            clip: true // Clip grid content
                            cacheBuffer: 400
                            reuseItems: true
                        onCurrentIndexChanged: {
                            if (listView.currentIndex !== currentIndex)
                                listView.currentIndex = currentIndex
                        }
                        delegate: Rectangle {
                            id: tile
                            Drag.active: false
                            Drag.dragType: Drag.Automatic
                            Drag.mimeData: { "application/x-kasset-assets": JSON.stringify(AppState.selectedAssetIds && AppState.selectedAssetIds.length>0 ? AppState.selectedAssetIds : [tile.assetId]) }
                            property bool selected: AppState.selectedAssetIds && AppState.selectedAssetIds.indexOf(tile.assetId) !== -1
                            color: selected ? "#2f3a4a" : (GridView.isCurrentItem ? "#2a2a2a" : "#121212")
                            width: filesGrid.cellWidth - 8; height: filesGrid.cellHeight - 8
                            border.color: "#333"; radius: 4

                            required property int index
                            required property int assetId
                            required property string fileName
                            required property string filePath
                            required property var fileSize
                            required property string thumbnailPath
                            required property string fileType
                            required property var lastModified
                            property int thumbnailVersion: 0
                            property string thumbnailSource: ""

                            function updateThumbnailSource() {
                                if (thumbnailPath && thumbnailPath.length > 0) {
                                    thumbnailVersion += 1
                                    thumbnailSource = toFileUrl(thumbnailPath) + "?v=" + thumbnailVersion
                                } else {
                                    thumbnailSource = ""
                                }
                            }

                            Component.onCompleted: updateThumbnailSource()
                            onThumbnailPathChanged: updateThumbnailSource()

                            Column {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 4
                                clip: true

                                // Thumbnail area
                                Rectangle {
                                    width: parent.width
                                    height: 70
                                    color: "#0e0e0e"
                                    radius: 3

                                    Image {
                                        id: thumbnail
                                        anchors.fill: parent
                                        anchors.margins: 2
                                        source: tile.thumbnailSource
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                        cache: false
                                        visible: status === Image.Ready

                                        // Smooth scaling for better quality
                                        smooth: true
                                        mipmap: true
                                    }

                                    // Loading indicator
                                    Text {
                                        anchors.centerIn: parent
                                        text: thumbnail.status === Image.Loading ? "..." :
                                              thumbnail.status === Image.Error ? "âœ—" :
                                              !tile.thumbnailPath ? (tile.fileName && tile.fileName.indexOf(".") >= 0 ? tile.fileName.split(".").pop().toUpperCase() : "?") : ""
                                        color: "#888"
                                        font.pixelSize: thumbnail.status === Image.Error ? 24 : 12
                                        visible: thumbnail.status !== Image.Ready
                                    }
                                }

                                Column {
                                    spacing: 2
                                    width: parent.width
                                    Text {
                                        text: highlightMatch(tile.fileName, "#f5f5f5")
                                        textFormat: Text.RichText
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    Text {
                                        text: (tile.fileType ? tile.fileType.toUpperCase() : "UNKNOWN") + " | " + (tile.fileSize > 0 ? humanSize(tile.fileSize) : "N/A")
                                        textFormat: Text.PlainText
                                        color: "#b0b0b0"
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    Text {
                                        text: tile.lastModified ? "Modified: " + humanDateTime(tile.lastModified) : "Modified: N/A"
                                        textFormat: Text.PlainText
                                        color: "#808080"
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    Text {
                                        id: tagsLine
                                        // Recompute when version increments
                                        property int version: 0
                                        text: (function(){ var t = assetsModel.tagsForAsset(tile.assetId); return t && t.length>0 ? ("Tags: " + t.join(", ")) : "Tags: none"; })()
                                        textFormat: Text.PlainText
                                        color: "#666666"
                                        font.pixelSize: 10
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                    Connections {
                                        target: assetsModel
                                        function onTagsChangedForAsset(assetId) {
                                            if (assetId === tile.assetId) {
                                                tagsLine.version += 1
                                                // Trigger text re-evaluation
                                                tagsLine.text = (function(){ var t = assetsModel.tagsForAsset(tile.assetId); return t && t.length>0 ? ("Tags: " + t.join(", ")) : "Tags: none"; })()
                                            }
                                        }
                                    }
                                }
                            }
                            property bool started: false
                            property real sx: 0; property real sy: 0
                            // Left-click and drag handling
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onPressed: function(mouse) {
                                    var wasSelected = AppState.selectedAssetId === tile.assetId
                                    if (mouse.modifiers & Qt.ControlModifier) {
                                        // toggle multi-select
                                        var arr = AppState.selectedAssetIds.slice(0)
                                        var idx = arr.indexOf(tile.assetId)
                                        if (idx >= 0) arr.splice(idx,1); else arr.push(tile.assetId)
                                        AppState.selectedAssetIds = arr
                                        if (AppState.selectionAnchorIndex < 0) AppState.selectionAnchorIndex = index
                                    } else if (mouse.modifiers & Qt.ShiftModifier) {
                                        var anchor = AppState.selectionAnchorIndex >= 0 ? AppState.selectionAnchorIndex : filesGrid.currentIndex
                                        var ids = selectRange(anchor, index)
                                        AppState.selectedAssetIds = ids
                                    } else {
                                        AppState.selectedAssetIds = [ tile.assetId ]
                                        AppState.selectionAnchorIndex = index
                                    }
                                    AppState.selectedAssetId = tile.assetId
                                    AppState.selectedFileName = tile.fileName
                                    AppState.selectedFilePath = tile.filePath
                                    AppState.selectedFileSize = tile.fileSize
                                    AppState.previewIndex = index
                                    AppState.selectedFileType = tile.fileType
                                    AppState.selectedFileModified = tile.lastModified ? Qt.formatDateTime(tile.lastModified, Qt.DefaultLocaleShortDate) : ""
                                    tile.sx = mouse.x
                                    tile.sy = mouse.y
                                    tile.started = false
                                    filesGrid.currentIndex = index
                                    if (!wasSelected) {
                                        LogManager.addLog("Selected asset: " + (tile.fileName || tile.filePath))
                                    }
                                }
                                onPositionChanged: function(mouse) {
                                    if (pressed && !tile.started) {
                                        if (Math.abs(mouse.x - tile.sx) > 6 || Math.abs(mouse.y - tile.sy) > 6) {
                                            tile.started = true
                                            // Start internal drag so folder DropArea can accept
                                            tile.Drag.active = true
                                            LogManager.addLog("Internal drag for selected assets started")
                                        }
                                    }
                                }
                                onDoubleClicked: function(mouse) { openPreview(index) }
                            }
                            // Right-click context menu
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: function(event) {
                                    // Select the asset first if not already selected
                                    if (AppState.selectedAssetIds.indexOf(tile.assetId) === -1) {
                                        AppState.selectedAssetIds = [tile.assetId]
                                        AppState.selectedAssetId = tile.assetId
                                        AppState.selectedFileName = tile.fileName
                                        AppState.selectedFilePath = tile.filePath
                                        AppState.selectedFileSize = tile.fileSize
                                        AppState.previewIndex = index
                                        AppState.selectedFileType = tile.fileType
                                        AppState.selectedFileModified = tile.lastModified ? Qt.formatDateTime(tile.lastModified, Qt.DefaultLocaleShortDate) : ""
                                        filesGrid.currentIndex = index
                                    }
                                    assetMenu.popup(event.point.position.x, event.point.position.y)
                                }
                            }
                            // Per-asset context menu (adds Assign Tag submenu)
                            Menu {
                                id: assetMenu
                                background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.border }
                                MenuItem { text: "Show in Explorer"; onTriggered: { DragUtils.showInExplorer(tile.filePath); LogManager.addLog("Show in Explorer: " + tile.filePath) } }
                                MenuItem { text: "Move to selected folder"; enabled: AppState.selectedFolderId>0; onTriggered: {
                                    LogManager.addLog("Move assets request to folder " + AppState.selectedFolderId)
                                    var ok = assetsModel.moveAssetsToFolder(AppState.selectedAssetIds.length>0?AppState.selectedAssetIds:[tile.assetId], AppState.selectedFolderId)
                                    LogManager.addLog(ok ? "Move succeeded" : "Move failed")
                                } }
                                MenuSeparator{}
                                Menu {
                                    title: "Assign Tag"
                                    // Use Instantiator to create MenuItems dynamically
                                    Instantiator {
                                        model: TagsModel { }
                                        delegate: MenuItem {
                                            required property int id
                                            required property string name
                                            text: name
                                            onTriggered: {
                                                var ids = AppState.selectedAssetIds.length>0?AppState.selectedAssetIds:[tile.assetId]
                                                var ok = assetsModel.assignTags(ids, [id])
                                                LogManager.addLog(ok ? ("Assigned tag '" + name + "' to " + ids.length + " asset(s)") : "Tag assign failed")
                                            }
                                        }
                                        onObjectAdded: (index, object) => assetMenu.insertItem(index, object)
                                        onObjectRemoved: (index, object) => assetMenu.removeItem(object)
                                    }
                                    MenuSeparator { }
                                    MenuItem {
                                        text: "New Tag…"
                                        onTriggered: {
                                            // Open the panel's add tag dialog if visible, else log
                                            LogManager.addLog("Use the Tags panel '+' to create a new tag")
                                        }
                                    }
                                }
                                MenuSeparator{}
                                MenuItem { text: "Set Rating → 0"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds.length>0?AppState.selectedAssetIds:[tile.assetId], 0) }
                                MenuItem { text: "Set Rating → 1"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds.length>0?AppState.selectedAssetIds:[tile.assetId], 1) }
                                MenuItem { text: "Set Rating → 2"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds.length>0?AppState.selectedAssetIds:[tile.assetId], 2) }
                                MenuItem { text: "Set Rating → 3"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds.length>0?AppState.selectedAssetIds:[tile.assetId], 3) }
                                MenuItem { text: "Set Rating → 4"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds.length>0?AppState.selectedAssetIds:[tile.assetId], 4) }
                                MenuItem { text: "Set Rating → 5"; onTriggered: assetsModel.setAssetsRating(AppState.selectedAssetIds.length>0?AppState.selectedAssetIds:[tile.assetId], 5) }
                                MenuSeparator{}
                                MenuItem { text: "Remove from App"; onTriggered: assetsModel.removeAssets(AppState.selectedAssetIds.length>0?AppState.selectedAssetIds:[tile.assetId]) }
                            }

                            // Context menu for empty space (right-click) without intercepting tile menus
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: function(ev) {
                                    // Determine if click is over an item
                                    var ix = filesGrid.indexAt(ev.point.position.x + filesGrid.contentX, ev.point.position.y + filesGrid.contentY)
                                    if (ix < 0) {
                                        if (AppState.selectedAssetIds && AppState.selectedAssetIds.length>0) bulkMenu.popup()
                                    }
                                }
                                grabPermissions: TapHandler.CanBeGrabbedByItems
                            }
                        }
                        Keys.onPressed: function(e) {
                            var cols = Math.max(1, Math.floor(width / cellWidth))
                            if (e.key === Qt.Key_Left) { currentIndex = Math.max(0, currentIndex - 1); e.accepted = true }
                            else if (e.key === Qt.Key_Right) { currentIndex = Math.min(count-1, currentIndex + 1); e.accepted = true }
                            else if (e.key === Qt.Key_Up) { currentIndex = Math.max(0, currentIndex - cols); e.accepted = true }
                            else if (e.key === Qt.Key_Down) { currentIndex = Math.min(count-1, currentIndex + cols); e.accepted = true }
                            else if (e.key === Qt.Key_Space || e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                                openPreview(currentIndex)
                                e.accepted = true
                            }
                        }
                    }
                        ListView {
                            id: listView
                        visible: viewMode === "list"
                        width: parent.width
                        height: parent.height - assetSearchField.height - viewToggleRow.height - 30
                        model: assetsModel
                        clip: true
                        currentIndex: filesGrid.currentIndex
                        focus: viewMode === "list"
                        onCurrentIndexChanged: {
                            if (filesGrid.currentIndex !== currentIndex)
                                filesGrid.currentIndex = currentIndex
                        }
                        // Context menu for empty space in list view
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: function(ev) {
                                var ix = listView.indexAt(ev.point.position.x + listView.contentX, ev.point.position.y + listView.contentY)
                                if (ix < 0) {
                                    if (AppState.selectedAssetIds && AppState.selectedAssetIds.length>0) bulkMenu.popup()
                                }
                            }
                            grabPermissions: TapHandler.CanBeGrabbedByItems
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "#101010"
                            z: -1
                        }
header: Rectangle {
                            height: 32
                            color: "#202020"
                            border.color: "#333"
                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 24
                                    Text { text: "Name"; color: "#ccc"; width: 220 }
                                    Text { text: "Type"; color: "#888"; width: 80 }
                                    Text { text: "Size"; color: "#888"; width: 100 }
                                    Text { text: "Modified"; color: "#888"; width: 140 }
                                    // Clamp to avoid negative/overflow widths during resizes
                                    Text { text: "Location"; color: "#888"; width: Math.max(80, listView.width - 540) }
                                }
                        }
                        delegate: Rectangle {
                            width: listView.width
                            height: 40
                            color: ListView.isCurrentItem ? "#2a2a2a" : "#121212"
                            required property int assetId
                            required property string fileName
                            required property string filePath
                            required property var fileSize
                            required property string fileType
                            required property var lastModified
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    var wasSelected = AppState.selectedAssetId === assetId
                                    listView.currentIndex = index
                                    filesGrid.currentIndex = index
                                    AppState.selectedAssetId = assetId
                                    AppState.selectedFileName = fileName
                                    AppState.selectedFilePath = filePath
                                    AppState.selectedFileSize = fileSize
                                    AppState.selectedFileType = fileType
                                    AppState.selectedFileModified = lastModified ? Qt.formatDateTime(lastModified, Qt.DefaultLocaleShortDate) : ""
                                    AppState.previewIndex = index
                                    if (!wasSelected) {
                                        LogManager.addLog("Selected asset: " + (fileName || filePath))
                                    }
                                }
                                onDoubleClicked: {
                                    AppState.selectedAssetId = assetId
                                    AppState.selectedFileName = fileName
                                    AppState.selectedFilePath = filePath
                                    AppState.selectedFileSize = fileSize
                                    AppState.selectedFileType = fileType
                                    AppState.selectedFileModified = lastModified ? Qt.formatDateTime(lastModified, Qt.DefaultLocaleShortDate) : ""
                                    AppState.previewIndex = index
                                    openPreview(index)
                                }
                            }
                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 24
                                    Text {
                                        text: highlightMatch(fileName, "#f5f5f5")
                                        textFormat: Text.RichText
                                        elide: Text.ElideRight
                                        width: 220
                                    }
                                    Text {
                                        text: fileType ? fileType.toUpperCase() : ""
                                        color: "#bbbbbb"
                                        width: 80
                                    }
                                    Text {
                                        text: humanSize(fileSize)
                                        color: "#bbbbbb"
                                        width: 100
                                    }
                                    Text {
                                        text: lastModified ? humanDateTime(lastModified) : ""
                                        color: "#999"
                                        width: 140
                                    }
                                    Text {
                                        text: highlightMatch(filePath, "#666")
                                        textFormat: Text.RichText
                                        elide: Text.ElideRight
                                        width: Math.max(80, listView.width - 540)
                                    }
                            }
                        }
                    }
                }
            }
            // Right-side Info drawer
            Rectangle {
                id: infoPane
                width: AppState.infoOpen ? 360 : 0
                height: parent.height
                color: Theme.surface; radius: Theme.radius; border.color: Theme.border; border.width: 1
                Behavior on width { NumberAnimation { duration: 120 } }
                clip: true
                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Label { text: "Info"; color: "#EEE"; font.bold: true }

                    Loader {
                        id: infoPanelPreviewLoader
                        width: parent.width - 4
                        height: AppState.selectedFilePath && AppState.selectedFilePath.length > 0 ? 200 : 0
                        visible: height > 0
                        onSourceComponentChanged: {
                            if (item && item.stop) {
                                item.stop()
                            }
                        }
                        onVisibleChanged: {
                            if (!visible && item && item.stop) {
                                item.stop()
                            }
                        }
                        sourceComponent: !AppState.selectedFilePath || AppState.selectedFilePath.length === 0 ? null :
                            (ThumbnailGenerator.isImageFile(AppState.selectedFilePath) ? imagePreviewComponent : videoPreviewComponent)
                    }

                    Text {
                        text: AppState.selectedFileName || "Select an asset"
                        color: "#ddd"
                        elide: Text.ElideRight
                        font.bold: AppState.selectedAssetId > 0
                    }
                    Text {
                        text: AppState.selectedFilePath
                        color: "#999"
                        wrapMode: Text.WrapAnywhere
                        visible: AppState.selectedFilePath && AppState.selectedFilePath.length > 0
                    }
                    Row {
                        spacing: 12
                        visible: AppState.selectedAssetId > 0
                        Text { text: "Size: " + (AppState.selectedFileSize > 0 ? humanSize(AppState.selectedFileSize) : "—"); color: "#aaa" }
                        Text { text: "Type: " + (AppState.selectedFileType ? AppState.selectedFileType.toUpperCase() : "—"); color: "#aaa" }
                    }
                    Text {
                        text: "Modified: " + (AppState.selectedFileModified && AppState.selectedFileModified.length > 0 ? AppState.selectedFileModified : "—")
                        color: "#888"
                        visible: AppState.selectedAssetId > 0
                    }
                    Text {
                        text: AppState.selectedAssetId > 0 ? (function(){ var t = assetsModel.tagsForAsset(AppState.selectedAssetId); return t && t.length>0 ? ("Tags: " + t.join(", ")) : "Tags: none"; })() : ""
                        color: "#888"
                        visible: AppState.selectedAssetId > 0
                    }

                    Row {
                        spacing: 8
                        visible: AppState.selectedAssetId > 0
                        ShButton {
                            text: "Reveal in Explorer"
                            enabled: AppState.selectedFilePath && AppState.selectedFilePath.length > 0
                            onClicked: DragUtils.showInExplorer(AppState.selectedFilePath)
                        }
                        ShButton {
                            text: ThumbnailGenerator.isVideoFile(AppState.selectedFilePath) ? "Play Preview" : "View Preview"
                            enabled: AppState.selectedAssetId > 0
                            onClicked: openPreview(AppState.previewIndex)
                        }
                    }

                    Component {
                        id: imagePreviewComponent
                        Rectangle {
                            width: infoPanelPreviewLoader.width
                            height: infoPanelPreviewLoader.height
                            color: "#111"
                            radius: 6
                            border.color: "#222"
                            border.width: 1
                            Image {
                                anchors.fill: parent
                                anchors.margins: 6
                                source: toFileUrl(AppState.selectedFilePath)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                smooth: true
                                mipmap: true
                            }
                        }
                    }

                    Component {
                        id: videoPreviewComponent
                        Item {
                            width: infoPanelPreviewLoader.width
                            height: infoPanelPreviewLoader.height
                            function msToTime(ms) {
                                var s = Math.max(0, Math.floor(ms/1000));
                                var m = Math.floor(s/60); var ss = (s%60).toString().padStart(2,'0');
                                return m + ":" + ss
                            }
                            Rectangle {
                                anchors.fill: parent
                                color: "#111"
                                radius: 6
                                border.color: "#222"
                                border.width: 1
                            }
                            AudioOutput { id: previewAudio; muted: true; volume: 0.4 }
                            VideoOutput { id: previewVideo; anchors.fill: parent; anchors.margins: 6; fillMode: VideoOutput.PreserveAspectFit }
                            MediaPlayer {
                                id: previewPlayer
                                source: toFileUrl(AppState.selectedFilePath)
                                autoPlay: true
                                loops: MediaPlayer.Infinite
                                audioOutput: previewAudio
                                videoOutput: previewVideo
                            }
                            // Keep the sidebar preview lightweight to avoid UI stalls; full controls are in the overlay
                            Row {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: 8
                                spacing: 8
                                    ShButton { text: previewPlayer.playbackState === MediaPlayer.PlayingState ? "Pause" : "Play"; onClicked: previewPlayer.playbackState === MediaPlayer.PlayingState ? previewPlayer.pause() : previewPlayer.play() }
                                    ShButton { text: previewAudio.muted ? "Unmute" : "Mute"; onClicked: previewAudio.muted = !previewAudio.muted }
                            }
                        }
                    }
                }
            }

        }

    Shortcut {
        sequences: [ Qt.Key_Space ]
        context: Qt.ApplicationShortcut
        enabled: !AppState.previewOpen && !assetSearchField.activeFocus
        onActivated: openPreview()
    }

    Rectangle {
        id: previewOverlay
        anchors.fill: parent
        color: "#c0000000"
        visible: AppState.previewOpen
        z: 100
        focus: visible
        onVisibleChanged: {
            if (!visible && fullPreviewLoader.item && fullPreviewLoader.item.stop) {
                fullPreviewLoader.item.stop()
            }
        }
        Keys.onPressed: {
            if (event.key === Qt.Key_Escape) {
                closePreview()
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                changePreview(1)
                event.accepted = true
            } else if (event.key === Qt.Key_Left) {
                changePreview(-1)
                event.accepted = true
            }
        }
        MouseArea {
            anchors.fill: parent
            z: -1
            onClicked: closePreview()
        }
        Rectangle {
            id: previewContainer
            z: 1
            width: parent.width * 0.75
            height: parent.height * 0.75
            anchors.centerIn: parent
            color: "#111111"
            radius: 10
            border.color: "#2b2b2b"
            border.width: 1
            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                Loader {
                    id: fullPreviewLoader
                    anchors.fill: parent
                    onSourceComponentChanged: {
                        if (item && item.stop) {
                            item.stop()
                        }
                    }
                    sourceComponent: AppState.previewMediaType === "video" ? videoPreviewFullComponent : imagePreviewFullComponent
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 16
                    ShButton { text: "Prev"; enabled: AppState.previewIndex > 0; onClicked: changePreview(-1) }
                    ShButton { text: "Close"; onClicked: closePreview() }
                    ShButton { text: "Next"; enabled: assetsModel && AppState.previewIndex < assetsModel.rowCount() - 1; onClicked: changePreview(1) }
                }
            }
        }
    }

    Component {
        id: imagePreviewFullComponent
        Item {
            anchors.fill: parent
            function stop() {}
            Image {
                anchors.fill: parent
                source: toFileUrl(AppState.previewSource)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                mipmap: true
            }
        }
    }

    Component {
        id: videoPreviewFullComponent
        Item {
            anchors.fill: parent
            function stop() { previewPlayer.stop() }
            function msToTime(ms) {
                var s = Math.max(0, Math.floor(ms/1000));
                var m = Math.floor(s/60); var ss = (s%60).toString().padStart(2,'0');
                return m + ":" + ss
            }
            AudioOutput { id: previewAudioFull; volume: 0.6 }
            VideoOutput { id: previewVideoFull; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit }
            MediaPlayer {
                id: previewPlayer
                source: toFileUrl(AppState.previewSource)
                autoPlay: true
                loops: MediaPlayer.Infinite
                audioOutput: previewAudioFull
                videoOutput: previewVideoFull
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 64
                color: "#141414"
                border.color: "#222"
                Row {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    ShButton { text: previewPlayer.playbackState === MediaPlayer.PlayingState ? "Pause" : "Play"; onClicked: previewPlayer.playbackState === MediaPlayer.PlayingState ? previewPlayer.pause() : previewPlayer.play() }
                    ShButton { text: "Stop"; onClicked: previewPlayer.stop() }
                    Label { text: msToTime(previewPlayer.position); color: "#ccc" }
Slider {
                        id: fullTimeline
                        width: parent.width - 360
                        from: 0; to: Math.max(1, previewPlayer.duration)
                        value: previewPlayer.position
                        onMoved: previewPlayer.position = value
                    }
                    Label { text: msToTime(previewPlayer.duration); color: "#ccc" }
                    ShButton { text: previewAudioFull.muted ? "Unmute" : "Mute"; onClicked: previewAudioFull.muted = !previewAudioFull.muted }
                }
            }
        }
    }

        // Folder dialogs
        Dialog {
            id: newFolderDialog
            title: "New Folder"
            modal: true
            width: 400
            contentItem: Column {
                spacing: 8
                padding: 12
TextField { id: newFolderNameField; placeholderText: "Folder name"; text: AppState.newFolderName; onTextChanged: AppState.newFolderName = text; selectByMouse: true; focus: true }
            }
            footer: DialogButtonBox {
                standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
                onAccepted: {
                if (AppState.newFolderName && AppState.newFolderName.length>0) {
                    var createdId = folderModel.createFolder(AppState.newFolderParentId, AppState.newFolderName)
                    LogManager.addLog(createdId > 0 ? "Folder created: " + AppState.newFolderName : "Folder create failed")
                }
                AppState.newFolderName = ""
                newFolderDialog.close()
            }
                onRejected: newFolderDialog.close()
            }
        }
        Dialog {
            id: renameFolderDialog
            title: "Rename Folder"
            modal: true
            width: 400
            contentItem: Column {
                spacing: 8
                padding: 12
TextField { id: renameFolderNameField; placeholderText: "New name"; text: AppState.renameFolderName; onTextChanged: AppState.renameFolderName = text; selectByMouse: true; focus: true }
            }
            footer: DialogButtonBox {
                standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
                onAccepted: {
                if (AppState.renameFolderName && AppState.renameFolderName.length>0) {
                    var renamed = folderModel.renameFolder(AppState.renameFolderId, AppState.renameFolderName)
                    LogManager.addLog(renamed ? "Folder renamed to " + AppState.renameFolderName : "Folder rename failed")
                }
                renameFolderDialog.close()
            }
                onRejected: renameFolderDialog.close()
            }
        }
        Dialog {
            id: deleteConfirmDialog
            title: "Delete Folder"
            modal: true
            width: 500
            contentItem: Column {
                spacing: 8
                padding: 12
                Text { text: "Delete this virtual folder? Physical files will NOT be deleted.\nThis action only removes the folder from the virtual structure."; color: "#ddd"; wrapMode: Text.Wrap; width: parent.width }
            }
            footer: DialogButtonBox {
                standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
                onAccepted: {
                var deleted = folderModel.deleteFolder(AppState.deleteFolderId)
                LogManager.addLog(deleted ? "Folder deleted (id " + AppState.deleteFolderId + ")" : "Folder delete failed")
                deleteConfirmDialog.close()
            }
                onRejected: deleteConfirmDialog.close()
            }
        }


                // Status line
                Text { text: AppState.lastDropMessage; color: "#AAA" }
            }
        }

        // Tab 1: Log
        Item {
            Rectangle {
                anchors.fill: parent
                color: Theme.surfaceAlt
                radius: Theme.radius
                border.color: Theme.border
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Row {
                        width: parent.width
                        spacing: 8

                        Label {
                            text: "Application Log"
                            color: "#EEE"
                            font.bold: true
                            font.pixelSize: 16
                        }

                        Item { width: parent.width - 200; height: 1 }

                        ShButton {
                            text: "Clear Log"
                            onClicked: {
                                LogManager.clear()
                                LogManager.addLog("Log cleared by user")
                            }
                        }
                    }

                    ScrollView {
                        width: parent.width
                        height: parent.height - 40
                        clip: true

                        ListView {
                            id: logView
                            anchors.fill: parent
                            model: LogManager.logs
                            spacing: 2

                            delegate: Rectangle {
                                width: logView.width
                                height: logText.contentHeight + 8
                                property string entryText: modelData !== undefined ? modelData : ""
                                color: {
                                    if (entryText.indexOf("[ERROR]") !== -1 || entryText.indexOf("[FATAL]") !== -1) return "#331111"
                                    if (entryText.indexOf("[WARN]") !== -1) return "#332211"
                                    return "transparent"
                                }

                                TextEdit {
                                    id: logText
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.margins: 4
                                    readOnly: true
                                    wrapMode: TextEdit.Wrap
                                    selectByMouse: true
                                    text: entryText
                                    color: {
                                        if (entryText.indexOf("[ERROR]") !== -1 || entryText.indexOf("[FATAL]") !== -1) return "#FF6666"
                                        if (entryText.indexOf("[WARN]") !== -1) return "#FFAA66"
                                        if (entryText.indexOf("[DEBUG]") !== -1) return "#888888"
                                        return "#CCCCCC"
                                    }
                                    font.family: "Consolas, Courier New, monospace"
                                    font.pixelSize: 11
                                    cursorVisible: false
                                    focus: false
                                }
                            }

                            // Auto-scroll to bottom when new logs arrive
                            onCountChanged: {
                                Qt.callLater(function() {
                                    logView.positionViewAtEnd()
                                })
                            }
                        }
                    }
                }
            }
        }

        // Tab 2: Settings
        Item {
            Rectangle {
                anchors.fill: parent
                color: "#171717"
                radius: 6
                border.color: "#333"
                border.width: 1

                Column {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 20

                    Label {
                        text: "Settings"
                        color: "#EEE"
                        font.bold: true
                        font.pixelSize: 18
                    }

                    GroupBox {
                        title: "Debug Tools"
                        width: parent.width

                        Column {
                            spacing: 8
                            width: parent.width

                            Label {
                                text: "Use these helpers to exercise LogManager and ProgressManager from QML."
                                color: "#888"
                                wrapMode: Text.WordWrap
                            }

                        ShButton {
                            text: "Add Test Log Entry"
                            onClicked: LogManager.addLog("QML test log @" + new Date().toLocaleTimeString(), "DEBUG")
                        }

                            Row {
                                spacing: 8

                                ShButton {
                                    text: "Start Test Progress"
                                    onClicked: {
                                        root.debugProgressStep = 0
                                        ProgressManager.start("QML progress test", 3)
                                    }
                                }
                                ShButton {
                                    text: "Advance Progress"
                                    enabled: root.progressActive
                                    onClicked: {
                                        root.debugProgressStep = Math.min(root.progressTotal, root.debugProgressStep + 1)
                                        ProgressManager.update(root.debugProgressStep, "QML progress step " + root.debugProgressStep)
                                    }
                                }
                                ShButton {
                                    text: "Finish Progress"
                                    enabled: root.progressActive
                                    onClicked: ProgressManager.finish()
                                }
                            }

                            ShButton {
                                text: AppState.selectedFilePath && AppState.selectedFilePath.length > 0
                                      ? "Request Thumbnail For Selected Asset"
                                      : "Select an asset to request thumbnail"
                                enabled: AppState.selectedFilePath && AppState.selectedFilePath.length > 0
                                onClicked: {
                                    diagLog("Debug: requesting thumbnail for", AppState.selectedFilePath)
                                    ThumbnailGenerator.requestThumbnail(AppState.selectedFilePath)
                                    LogManager.addLog("Thumbnail requested: " + AppState.selectedFilePath)
                                }
                            }
                        }
                        visible: root.diagnosticsEnabled
                    }

                    GroupBox {
                        title: "Thumbnail Generation"
                        width: parent.width

                        Column {
                            spacing: 12
                            width: parent.width

                            Row {
                                spacing: 8
                                Label { text: "Thumbnail Size:"; color: "#CCC" }
                                Label { text: "256x256 (fixed)"; color: "#888" }
                            }

                            Row {
                                spacing: 8
                                Label { text: "Max Concurrent Threads:"; color: "#CCC" }
                                Label { text: "4 (fixed)"; color: "#888" }
                            }

                            Button {
                                text: "Clear Thumbnail Cache"
                                onClicked: {
                                    ThumbnailGenerator.clearCache()
                                    LogManager.addLog("Thumbnail cache cleared")
                                    assetsModel.reload()
                                }
                            }
                        }
                    }

                    GroupBox {
                        title: "Database"
                        width: parent.width

                        Column {
                            spacing: 12
                            width: parent.width

                            Row {
                                spacing: 8
                                Label { text: "Location:"; color: "#CCC" }
                                Label { text: "{appDir}/data/kasset.db"; color: "#888" }
                            }

                            Row {
                                spacing: 8
                                Button {
                                    text: "Purge Missing Files"
                                    onClicked: {
                                        var n = importer.purgeMissingAssets()
                                        LogManager.addLog("Purged missing assets: " + n)
                                        assetsModel.reload()
                                    }
                                }
                                Button {
                                    text: "Purge Autotest Assets"
                                    onClicked: {
                                        var n2 = importer.purgeAutotestAssets()
                                        LogManager.addLog("Purged autotest assets: " + n2)
                                        assetsModel.reload()
                                    }
                                }
                            }
                        }
                    }

                    Item { height: 20 }

                    Label {
                        text: "KAsset Manager Qt v0.1.0"
                        color: "#666"
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    // Progress bar at bottom
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 44
        color: Theme.surface
        border.color: Theme.border
        border.width: 1
        visible: root.progressActive

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Row {
                width: parent.width
                spacing: 8

                Label {
                    text: root.progressMessage
                    color: "#CCC"
                    font.pixelSize: 12
                }

                Item { width: parent.width - 300 }

                Label {
                    text: root.progressCurrent + " / " + root.progressTotal + " (" + root.progressPercent + "%)"
                    color: "#888"
                    font.pixelSize: 11
                }
            }

            ProgressBar {
                width: parent.width
                from: 0
                to: root.progressTotal
                value: root.progressCurrent
            }
        }
    }
}
























