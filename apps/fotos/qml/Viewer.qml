import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia

/// One picture or one video, as large as the window, with the rest of its
/// page a key away.
Rectangle {
    id: viewer

    property var paths: []
    property int index: 0
    /// Goes up when a file on show has new contents.
    property int epoch: 0
    property bool infoOpen: false
    readonly property string path: index >= 0 && index < paths.length ? paths[index] : ""
    readonly property var item: path !== "" && epoch >= 0 && App.library.revision >= 0 ? App.item(path) : ({})
    readonly property bool isVideo: item.video === true
    readonly property bool isAnimated: item.animated === true
    readonly property bool favorite: App.albums.revision >= 0 && path !== "" && App.albums.isFavorite(path)

    signal closed()
    signal menuRequested(string path)
    signal trashRequested(string path)
    signal said(string message)

    function show(list, at) {
        paths = list
        index = Math.max(0, Math.min(list.length - 1, at))
        visible = true
        forceActiveFocus()
    }
    function close() {
        if (!visible)
            return
        player.stop()
        visible = false
        paths = []
        closed()
    }
    function step(by) {
        const next = index + by
        if (next >= 0 && next < paths.length)
            index = next
    }
    /// The picture on show is gone: the next takes its place.
    function dropped(gone) {
        const left = paths.filter(path => !gone.includes(path))
        if (left.length === 0) {
            close()
            return
        }
        const at = Math.min(index, left.length - 1)
        paths = left
        index = at
    }
    function rotate(clockwise) {
        if (App.rotate(path, clockwise))
            zoom = 1
    }
    function togglePlay() {
        if (player.playbackState === MediaPlayer.PlayingState)
            player.pause()
        else
            player.play()
    }

    // --- Zoom: 1 fits the window ------------------------------------------------

    property real zoom: 1
    /// What 1:1 is, in units of «fits the window».
    readonly property real actual: fit.scale > 0 ? 1 / fit.scale : 1
    readonly property real maxZoom: Math.max(4, actual * 4)
    function zoomTo(value, x, y) {
        const next = Math.max(1, Math.min(maxZoom, value))
        if (next === zoom)
            return
        // The point under the pointer stays under the pointer.
        const cx = x ?? flick.width / 2
        const cy = y ?? flick.height / 2
        const fx = (flick.contentX + cx - canvas.x) / Math.max(1, canvas.width)
        const fy = (flick.contentY + cy - canvas.y) / Math.max(1, canvas.height)
        zoom = next
        flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, fx * canvas.width + canvas.x - cx))
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, fy * canvas.height + canvas.y - cy))
    }

    onPathChanged: {
        zoom = 1
        player.stop()
    }

    visible: false
    color: Theme.stage
    focus: visible

    // Clicks and the wheel stop here: the page underneath is not touched.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }

    Keys.onPressed: event => {
        event.accepted = true
        const ctrl = event.modifiers & Qt.ControlModifier
        switch (event.key) {
        case Qt.Key_Escape:
            if (win.visibility === Window.FullScreen)
                win.showNormal()
            else
                close()
            break
        case Qt.Key_Left:
        case Qt.Key_PageUp:
            step(-1)
            break
        case Qt.Key_Right:
        case Qt.Key_PageDown:
            step(1)
            break
        case Qt.Key_Home:
            index = 0
            break
        case Qt.Key_End:
            index = paths.length - 1
            break
        case Qt.Key_Space:
            if (isVideo)
                togglePlay()
            else
                step(1)
            break
        case Qt.Key_Plus:
        case Qt.Key_Equal:
            zoomTo(zoom * 1.25)
            break
        case Qt.Key_Minus:
            zoomTo(zoom / 1.25)
            break
        case Qt.Key_0:
            zoomTo(1)
            break
        case Qt.Key_1:
            zoomTo(actual)
            break
        case Qt.Key_F:
        case Qt.Key_F11:
            if (win.visibility === Window.FullScreen)
                win.showNormal()
            else
                win.showFullScreen()
            break
        case Qt.Key_I:
            infoOpen = !infoOpen
            break
        case Qt.Key_R:
            if (!isVideo && App.canRotate(path))
                rotate(!(event.modifiers & Qt.ShiftModifier))
            break
        case Qt.Key_L:
        case Qt.Key_Period:
            App.albums.toggleFavorite(path)
            break
        case Qt.Key_C:
            if (ctrl) {
                App.copy([path])
                said(qsTr("Copiado."))
            }
            break
        case Qt.Key_Delete:
        case Qt.Key_Backspace:
            trashRequested(path)
            break
        default:
            event.accepted = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // --- The bar ----------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 46
            color: Theme.chrome
            visible: win.visibility !== Window.FullScreen
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 4
                TButton {
                    symbol: "chevron-left"
                    size: 18
                    onClicked: viewer.close()
                }
                Column {
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    spacing: 1
                    Text {
                        width: parent.width
                        text: viewer.item.name ?? ""
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    Text {
                        width: parent.width
                        text: viewer.item.taken > 0 ? App.moment(viewer.item.taken) : ""
                        color: Theme.dim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                Text {
                    text: viewer.paths.length > 1 ? qsTr("%1 de %2").arg(viewer.index + 1).arg(viewer.paths.length) : ""
                    color: Theme.dim
                    font.pixelSize: 12
                    rightPadding: 8
                }
                TButton {
                    visible: !viewer.isVideo
                    symbol: "zoom-out"
                    active: viewer.zoom > 1
                    onClicked: viewer.zoomTo(viewer.zoom / 1.5)
                }
                TButton {
                    visible: !viewer.isVideo
                    symbol: "zoom-in"
                    active: viewer.zoom < viewer.maxZoom
                    onClicked: viewer.zoomTo(viewer.zoom * 1.5)
                }
                TButton {
                    visible: !viewer.isVideo
                    symbol: "rotate-cw"
                    active: viewer.path !== "" && viewer.epoch >= 0 && App.canRotate(viewer.path)
                    onClicked: viewer.rotate(true)
                }
                TButton {
                    symbol: viewer.favorite ? "heart-fill" : "heart"
                    tone: viewer.favorite ? Theme.love : Theme.text
                    onClicked: App.albums.toggleFavorite(viewer.path)
                }
                TButton {
                    symbol: "info"
                    checked: viewer.infoOpen
                    onClicked: viewer.infoOpen = !viewer.infoOpen
                }
                TButton {
                    symbol: "trash-2"
                    onClicked: viewer.trashRequested(viewer.path)
                }
                TButton {
                    symbol: "ellipsis"
                    onClicked: viewer.menuRequested(viewer.path)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // --- The stage ----------------------------------------------------------
            Item {
                id: stage
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                // How the picture fits: its size on screen at zoom 1.
                QtObject {
                    id: fit
                    readonly property real w: viewer.item.width > 0 ? viewer.item.width : (still.implicitWidth > 0 ? still.implicitWidth : stage.width)
                    readonly property real h: viewer.item.height > 0 ? viewer.item.height : (still.implicitHeight > 0 ? still.implicitHeight : stage.height)
                    // A small picture is not blown up to fill the window.
                    readonly property real scale: Math.min(1, flick.width / Math.max(1, w), flick.height / Math.max(1, h))
                }

                Flickable {
                    id: flick
                    anchors.fill: parent
                    visible: !viewer.isVideo
                    contentWidth: Math.max(width, canvas.width)
                    contentHeight: Math.max(height, canvas.height)
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: viewer.zoom > 1

                    Item {
                        id: canvas
                        width: Math.round(fit.w * fit.scale * viewer.zoom)
                        height: Math.round(fit.h * fit.scale * viewer.zoom)
                        x: Math.max(0, (flick.width - width) / 2)
                        y: Math.max(0, (flick.height - height) / 2)

                        Image {
                            id: still
                            anchors.fill: parent
                            visible: !viewer.isAnimated
                            // Read no larger than it is shown; closer in, the whole of it.
                            readonly property int reach: Math.ceil(Math.max(stage.width, stage.height) * Screen.devicePixelRatio / 512) * 512
                            source: viewer.visible && !viewer.isVideo && !viewer.isAnimated ? Theme.photo(viewer.path, viewer.item.mtime ?? 0) : ""
                            sourceSize: viewer.zoom > 1.01 ? Qt.size(8192, 8192) : Qt.size(reach, reach)
                            retainWhileLoading: true
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: viewer.zoom <= 1
                        }
                        AnimatedImage {
                            anchors.fill: parent
                            visible: viewer.isAnimated
                            source: viewer.visible && viewer.isAnimated ? "file://" + encodeURI(viewer.path).replace(/#/g, "%23").replace(/\?/g, "%3F") : ""
                            playing: visible
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                }
                BusyIndicator {
                    anchors.centerIn: parent
                    running: !viewer.isVideo && !viewer.isAnimated && still.status === Image.Loading && loadingLate.late
                    visible: running
                    Timer {
                        id: loadingLate
                        // Only what is slow gets a spinner.
                        property bool late: false
                        interval: 350
                        running: still.status === Image.Loading
                        onRunningChanged: late = false
                        onTriggered: late = true
                    }
                }
                Column {
                    anchors.centerIn: parent
                    spacing: 10
                    visible: !viewer.isVideo && !viewer.isAnimated && still.status === Image.Error
                    Sym {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "image"
                        size: 40
                        color: Theme.faint
                    }
                    Text {
                        text: qsTr("No se puede mostrar esta imagen.")
                        color: Theme.dim
                        font.pixelSize: 13
                    }
                }
                MouseArea {
                    anchors.fill: flick
                    visible: flick.visible
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    // Dragging belongs to the Flickable underneath.
                    propagateComposedEvents: true
                    onPressed: mouse => mouse.accepted = mouse.button === Qt.RightButton || viewer.zoom <= 1
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton)
                            viewer.menuRequested(viewer.path)
                    }
                    onDoubleClicked: mouse => viewer.zoomTo(viewer.zoom > 1 ? 1 : Math.max(2, viewer.actual), mouse.x, mouse.y)
                    // The wheel comes closer; with Shift it turns the page.
                    onWheel: wheel => {
                        if (wheel.angleDelta.y === 0)
                            return
                        if (wheel.modifiers & Qt.ShiftModifier)
                            viewer.step(wheel.angleDelta.y > 0 ? -1 : 1)
                        else
                            viewer.zoomTo(viewer.zoom * (wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15), wheel.x, wheel.y)
                    }
                }
                // A double click while zoomed in, where the area above lets presses through.
                TapHandler {
                    enabled: flick.visible && viewer.zoom > 1
                    onDoubleTapped: viewer.zoomTo(1)
                }

                // --- A video ----------------------------------------------------------
                Item {
                    anchors.fill: parent
                    visible: viewer.isVideo
                    MediaPlayer {
                        id: player
                        source: viewer.visible && viewer.isVideo ? "file://" + encodeURI(viewer.path).replace(/#/g, "%23").replace(/\?/g, "%3F") : ""
                        videoOutput: screen
                        audioOutput: AudioOutput {
                            id: sound
                            muted: App.setting("muted", false)
                        }
                        onErrorOccurred: (error, text) => viewer.said(qsTr("No se puede reproducir este vídeo."))
                    }
                    VideoOutput {
                        id: screen
                        anchors.fill: parent
                        anchors.bottomMargin: 54
                    }
                    // Until it plays, the frame that stands for it.
                    Image {
                        anchors.fill: screen
                        visible: player.playbackState === MediaPlayer.StoppedState
                        source: viewer.visible && viewer.isVideo ? Theme.photo(viewer.path, viewer.item.mtime ?? 0) : ""
                        sourceSize: Qt.size(1920, 1920)
                        fillMode: Image.PreserveAspectFit
                    }
                    MouseArea {
                        anchors.fill: screen
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton)
                                viewer.menuRequested(viewer.path)
                            else
                                viewer.togglePlay()
                        }
                        onWheel: wheel => viewer.step(wheel.angleDelta.y > 0 ? -1 : 1)
                    }
                    Rectangle {
                        anchors.centerIn: screen
                        visible: player.playbackState !== MediaPlayer.PlayingState
                        width: 64
                        height: 64
                        radius: 32
                        color: "#88000000"
                        Sym {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 2
                            name: "play-fill"
                            size: 26
                            color: "#ffffff"
                        }
                    }
                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        height: 54
                        spacing: 10
                        TButton {
                            symbol: player.playbackState === MediaPlayer.PlayingState ? "pause-fill" : "play-fill"
                            onClicked: viewer.togglePlay()
                        }
                        Text {
                            text: App.time(scrub.dragging ? scrub.dragValue * player.duration : player.position)
                            color: Theme.dim
                            font.pixelSize: 12
                            font.features: { "tnum": 1 }
                        }
                        Seek {
                            id: scrub
                            Layout.fillWidth: true
                            fill: Theme.tint
                            active: player.seekable
                            value: player.duration > 0 ? player.position / player.duration : 0
                            onReleased: value => player.position = value * player.duration
                        }
                        Text {
                            text: App.time(player.duration > 0 ? player.duration : (viewer.item.duration ?? 0))
                            color: Theme.dim
                            font.pixelSize: 12
                            font.features: { "tnum": 1 }
                        }
                        TButton {
                            symbol: sound.muted ? "volume-x" : "volume-2"
                            onClicked: {
                                sound.muted = !sound.muted
                                App.setSetting("muted", sound.muted)
                            }
                        }
                    }
                }

                // --- To the one before, to the one after ------------------------
                Repeater {
                    model: [-1, 1]
                    delegate: Rectangle {
                        required property int modelData
                        readonly property bool possible: viewer.index + modelData >= 0 && viewer.index + modelData < viewer.paths.length
                        visible: possible && viewer.zoom <= 1
                        x: modelData < 0 ? 12 : stage.width - width - 12
                        y: (stage.height - height) / 2 - (viewer.isVideo ? 27 : 0)
                        width: 36
                        height: 36
                        radius: 18
                        color: "#66000000"
                        opacity: arrowArea.containsMouse ? 1 : 0.45
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                        Sym {
                            anchors.centerIn: parent
                            name: modelData < 0 ? "chevron-left" : "chevron-right"
                            size: 20
                            color: "#ffffff"
                        }
                        MouseArea {
                            id: arrowArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: viewer.step(modelData)
                        }
                    }
                }
            }

            // --- What the file says of itself ------------------------------------
            Rectangle {
                Layout.fillHeight: true
                implicitWidth: 280
                visible: viewer.infoOpen && win.visibility !== Window.FullScreen
                color: Theme.chrome
                Rectangle {
                    width: 1
                    height: parent.height
                    color: Theme.border
                }
                Flickable {
                    anchors.fill: parent
                    anchors.margins: 16
                    contentHeight: infoColumn.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: infoColumn
                        width: parent.width
                        spacing: 12
                        Text {
                            text: qsTr("Información")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        Repeater {
                            model: viewer.infoOpen && viewer.path !== "" && viewer.epoch >= 0 ? App.details(viewer.path) : []
                            delegate: Column {
                                required property var modelData
                                width: infoColumn.width
                                spacing: 2
                                Text {
                                    text: modelData.label
                                    color: Theme.dim
                                    font.pixelSize: 11
                                }
                                Text {
                                    width: parent.width
                                    text: modelData.value
                                    color: Theme.text
                                    font.pixelSize: 13
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- The rest of the page, small -------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 62
            visible: viewer.paths.length > 1 && win.visibility !== Window.FullScreen
            color: Theme.chrome
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            ListView {
                id: strip
                anchors.fill: parent
                anchors.topMargin: 7
                anchors.bottomMargin: 6
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                orientation: ListView.Horizontal
                spacing: 3
                clip: true
                model: viewer.visible ? viewer.paths : []
                // The one on show sits in the middle, as far as the ends allow.
                readonly property real wanted: Math.max(0, Math.min(count * 51 - 3 - width, viewer.index * 51 + 24 - width / 2))
                onWantedChanged: contentX = wanted
                Behavior on contentX {
                    enabled: viewer.visible && !strip.moving
                    NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
                }
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: small
                    required property string modelData
                    required property int index
                    width: 48
                    height: strip.height
                    radius: 4
                    color: Theme.plate
                    clip: true
                    opacity: index === viewer.index ? 1 : 0.55
                    Image {
                        anchors.fill: parent
                        source: Theme.thumb(small.modelData, "s" + viewer.epoch)
                        sourceSize: Qt.size(96, 96)
                        fillMode: Image.PreserveAspectCrop
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: "transparent"
                        border.width: small.index === viewer.index ? 2 : 0
                        border.color: Theme.tint
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: viewer.index = small.index
                    }
                }
            }
        }
    }
}
