import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window

/// Quick Look: Space on a file shows it in a window of its own, sized to what
/// it shows, and Space again puts it away. One window serves the whole app and
/// the desktop's icons; opened from a Rabbit window it follows that window's
/// selection, so the arrows walk the folder.
Window {
    id: look

    property var urls: []
    property int index: 0
    /// Rabbit window it was opened from, if any.
    property QtObject source: null
    readonly property url current: index < urls.length ? urls[index] : ""
    property var info: ({})
    property string words
    property string folderSize
    property string appName
    property bool failed: false
    /// How the item is shown: Look::kindFor, less what turned out not to be showable.
    readonly property string kind: {
        if (info.name === undefined)
            return ""
        if (info.path === undefined)
            return info.isDir ? "folder" : "other"
        const kind = App.lookKind(info.mime ?? "", info.isDir === true)
        if (failed || (kind === "text" && words === ""))
            return "other"
        return kind
    }
    readonly property int chrome: header.height + footer.height
    readonly property real roomW: Math.max(480, Screen.width * 0.7)
    readonly property real roomH: Math.max(360, Screen.height * 0.78)

    title: info.name ?? qsTr("Quick Look")
    color: App.glass ? "transparent" : Theme.window
    minimumWidth: 360
    minimumHeight: 240
    width: 520
    height: 420
    visible: false
    onVisibleChanged: {
        App.announceLook(visible)
        if (!visible) {
            urls = []
            index = 0
            info = ({})
            words = ""
            source = null
        }
    }

    function same(a, b) {
        return String(a) === String(b)
    }
    /// What App.quickLook asks: show these, or put the window away if it shows them already.
    function request(newUrls, newSource, update) {
        if (update) {
            if (!visible)
                return
        } else if (visible && newUrls.length && same(newUrls[0], current)) {
            close()
            return
        }
        source = newSource ?? null
        urls = newUrls
        index = 0
        load()
        if (!visible)
            show()
        if (!update) {
            raise()
            requestActivate()
        }
    }
    function describe(url) {
        const facts = App.info(url)
        if (facts.path !== undefined || !source)
            return facts
        // Not a file of this disk (the Trash): what the folder's own listing says of it.
        const row = source.model.rowForUrl(url)
        if (row < 0)
            return facts
        const item = source.model.get(row)
        return { name: item.name, url: url, icon: item.icon, kind: item.kind, mime: item.mime, isDir: item.isDir, isApp: false, thumb: false, sizeShort: item.isDir ? "--" : item.sizeText, modified: item.modifiedText }
    }
    function load() {
        failed = false
        folderSize = ""
        const facts = describe(current)
        words = facts.path !== undefined && App.lookKind(facts.mime ?? "", facts.isDir === true) === "text" ? App.readText(facts.path, 400000) : ""
        info = facts
        if (facts.isDir && facts.path !== undefined)
            App.measureFolder(current)
        const apps = facts.isDir || facts.isApp || !facts.mime ? [] : App.appsFor(facts.mime)
        const preferred = apps.find(app => app.isDefault) ?? apps[0]
        appName = preferred ? preferred.name : ""
        fit()
    }
    /// The window takes the shape of what it shows, within most of the screen.
    function fit() {
        let w = 520
        let h = 420
        switch (kind) {
        case "image":
        case "animation": {
            const size = info.pixelSize
            if (size && size.width > 0 && size.height > 0) {
                const scale = Math.min(1, roomW / size.width, (roomH - chrome) / size.height)
                w = size.width * scale
                h = size.height * scale + chrome
            } else {
                w = 640
                h = 520
            }
            break
        }
        case "text":
            w = 760
            h = Math.min(roomH, 640)
            break
        case "pdf":
            w = Math.min(roomW, 780)
            h = roomH
            break
        case "video":
            w = Math.min(roomW, 880)
            h = w * 9 / 16 + chrome
            break
        case "audio":
            w = 480
            h = 340
            break
        }
        width = Math.round(Math.max(480, w))
        height = Math.round(Math.max(320, h))
    }
    /// The video said its real shape.
    function fitVideo(size) {
        if (kind !== "video" || size.width <= 0 || size.height <= 0)
            return
        const scale = Math.min(roomW / size.width, (roomH - chrome) / size.height, Math.max(1, 880 / size.width))
        width = Math.round(Math.max(480, size.width * scale))
        height = Math.round(Math.max(320, size.height * scale + chrome))
    }
    function step(delta) {
        if (urls.length > 1) {
            index = (index + delta + urls.length) % urls.length
            load()
        } else if (source) {
            const row = source.model.rowForUrl(current) + delta
            if (row >= 0 && row < source.model.count)
                source.model.selectOnly(row)
        }
    }
    /// The source window's selection moved: show what is selected now.
    function follow() {
        if (!visible || !source || urls.length !== 1 || source.model.selectedCount !== 1)
            return
        const selected = source.model.selectedUrls()[0]
        if (same(selected, current))
            return
        urls = [selected]
        index = 0
        load()
    }
    function openItem() {
        if (info.isDir && !info.isApp)
            App.requestWindow(current)
        else
            App.open(current, info.mime ?? "", info.isApp === true)
        close()
    }

    Connections {
        target: look.source ? look.source.model : null
        function onSelectionChanged() {
            look.follow()
        }
    }
    Connections {
        target: look.source
        function onClosing() {
            look.close()
        }
    }
    Connections {
        target: App
        function onFolderSized(url, text) {
            if (look.same(url, look.current))
                look.folderSize = text
        }
    }

    Shortcut { sequences: ["Space", "Escape", "Ctrl+W"]; onActivated: look.close() }
    Shortcut { sequences: ["Left", "Up"]; onActivated: look.step(-1) }
    Shortcut { sequences: ["Right", "Down"]; onActivated: look.step(1) }
    Shortcut { sequences: ["Return", "Enter", "Ctrl+O", "Ctrl+Down"]; onActivated: look.openItem() }

    Rectangle {
        anchors.fill: parent
        color: App.glass ? Qt.rgba(Theme.window.r, Theme.window.g, Theme.window.b, 0.86) : Theme.window
    }

    Item {
        id: header
        width: parent.width
        height: 46

        DragHandler {
            target: null
            onActiveChanged: if (active) look.startSystemMove()
        }
        TButton {
            id: closeButton
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            symbol: "x"
            onClicked: look.close()
        }
        Row {
            id: pager
            visible: look.urls.length > 1
            anchors.left: closeButton.right
            anchors.leftMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            TButton {
                symbol: "chevron-left"
                onClicked: look.step(-1)
            }
            TButton {
                symbol: "chevron-right"
                onClicked: look.step(1)
            }
        }
        Text {
            anchors.left: pager.visible ? pager.right : closeButton.right
            anchors.leftMargin: 10
            anchors.right: openButton.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: (look.info.name ?? "") + (look.urls.length > 1 ? "   " + qsTr("%1 of %2").arg(look.index + 1).arg(look.urls.length) : "")
            elide: Text.ElideMiddle
            color: Theme.text
            font.pixelSize: 13
            font.bold: true
        }
        Rectangle {
            id: openButton
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(openLabel.implicitWidth + 24, parent.width * 0.45)
            height: 26
            radius: 7
            color: openArea.containsMouse ? Theme.sideSelection : Theme.field
            Text {
                id: openLabel
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: look.appName !== "" ? qsTr("Open with %1").arg(look.appName) : qsTr("Open")
                color: Theme.text
                font.pixelSize: 12
            }
            MouseArea {
                id: openArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: look.openItem()
            }
        }
    }

    Item {
        id: body
        anchors.top: header.bottom
        anchors.bottom: footer.top
        width: parent.width
        clip: true

        Image {
            visible: look.kind === "image"
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            autoTransform: true
            mipmap: true
            sourceSize: Qt.size(Math.round(look.roomW * 2), Math.round(look.roomH * 2))
            source: visible ? look.current : ""
            onStatusChanged: if (status === Image.Error) look.failed = true
        }
        AnimatedImage {
            visible: look.kind === "animation"
            anchors.fill: parent
            fillMode: Image.PreserveAspectFit
            source: visible ? look.current : ""
            playing: visible
            onStatusChanged: if (status === Image.Error) look.failed = true
        }
        Flickable {
            id: wordsView
            visible: look.kind === "text"
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 4
            contentHeight: wordsText.height + 12
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            Text {
                id: wordsText
                width: wordsView.width - 14
                text: wordsView.visible ? look.words : ""
                onTextChanged: wordsView.contentY = 0
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.text
                font.family: "monospace"
                font.pixelSize: 12
                lineHeight: 1.25
            }
        }
        Loader {
            id: pdfLoader
            anchors.fill: parent
            active: look.kind === "pdf"
            source: "LookPdf.qml"
            onLoaded: item.source = Qt.binding(() => look.current)
            onStatusChanged: if (status === Loader.Error) look.failed = true
        }
        Connections {
            target: pdfLoader.item
            ignoreUnknownSignals: true
            function onBroken() {
                look.failed = true
            }
        }
        Loader {
            id: mediaLoader
            anchors.fill: parent
            active: look.kind === "audio" || look.kind === "video"
            source: "LookMedia.qml"
            onLoaded: {
                item.video = Qt.binding(() => look.kind === "video")
                item.icon = Qt.binding(() => look.info.icon ?? "")
                item.source = Qt.binding(() => look.current)
            }
            onStatusChanged: if (status === Loader.Error) look.failed = true
        }
        Connections {
            target: mediaLoader.item
            ignoreUnknownSignals: true
            function onShaped(size) {
                look.fitVideo(size)
            }
            function onBroken() {
                look.failed = true
            }
        }
        // A folder, or a file with nothing of its own to show: its picture and what is known of it.
        Column {
            visible: look.kind === "folder" || look.kind === "other"
            anchors.centerIn: parent
            width: parent.width - 48
            spacing: 6
            Image {
                readonly property int side: Math.max(96, Math.min(256, body.height - 150))
                anchors.horizontalCenter: parent.horizontalCenter
                width: side
                height: side
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                sourceSize: Qt.size(512, 512)
                source: !parent.visible || look.info.icon === undefined ? "" : (look.info.thumb ? "image://thumb/" + look.info.path + "?" + look.info.modifiedStamp : App.icons + look.info.icon)
                onStatusChanged: if (status === Image.Error && look.info.icon !== undefined) source = App.icons + look.info.icon
            }
            Item {
                width: 1
                height: 6
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: look.info.name ?? ""
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideMiddle
                color: Theme.text
                font.pixelSize: 17
                font.bold: true
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: look.info.isDir ? (look.folderSize !== "" ? look.info.kind + "  ·  " + look.folderSize : look.info.kind ?? "") : (look.info.kind ?? "") + "  ·  " + (look.info.sizeShort ?? "")
                color: Theme.dim
                font.pixelSize: 13
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                visible: look.info.modified !== undefined
                text: qsTr("Modified %1").arg(look.info.modified ?? "")
                color: Theme.dim
                font.pixelSize: 12
            }
        }
    }

    Item {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        height: 30
        Text {
            anchors.centerIn: parent
            width: parent.width - 32
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            visible: look.kind !== "folder" && look.kind !== "other"
            text: {
                const facts = [look.info.kind, look.info.sizeShort]
                if (look.info.dimensions)
                    facts.push(look.info.dimensions)
                if (pdfLoader.item && pdfLoader.item.pages > 0)
                    facts.push(pdfLoader.item.pages === 1 ? qsTr("1 page") : qsTr("%1 pages").arg(pdfLoader.item.pages))
                facts.push(look.info.modified)
                return facts.filter(fact => fact).join("  ·  ")
            }
            color: Theme.dim
            font.pixelSize: 11
        }
    }
}
