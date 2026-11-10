import QtQuick
import QtQuick.Effects
import Quickshell
import ".."
import "../services"

// One entry of the Desktop folder: its icon (or the picture itself) over a
// two-line name. Press selects, drag moves the whole selection, double click
// opens, right click asks Desktop for the menu.
Item {
    id: icon

    required property var modelData
    required property var desk
    required property Item field

    readonly property string name: modelData.name
    readonly property var base: desk.shown[name] || null
    readonly property bool placed: base !== null
    readonly property bool selected: desk.selected[name] === true
    readonly property bool carried: desk.dragging && selected
    readonly property bool renaming: desk.renaming === name
    readonly property Region region: Region {
        x: (icon.base ? icon.base[0] : 0) + hit.x
        y: (icon.base ? icon.base[1] : 0) + hit.y
        width: hit.width
        height: hit.height
    }
    // Off until the icon has been shown once, so it doesn't fly in from the corner.
    property bool settled: false
    property bool thumbFailed: false


    function fileUrl(path) { return "file://" + path.split("/").map(encodeURIComponent).join("/") }

    // Folders and files of no known type are drawn as Wonderland Finder draws
    // them (icons/ holds its artwork), so the desktop and its windows match.
    readonly property string source: {
        if (modelData.thumb && !thumbFailed) return fileUrl(modelData.thumb)
        if (modelData.kind === "dir") return Qt.resolvedUrl("icons/folder.svg")
        for (const n of modelData.icons) {
            if (n === "unknown") break
            if (n.startsWith("/")) return fileUrl(n)
            const p = Quickshell.iconPath(n, true)
            if (p) return p
        }
        return Qt.resolvedUrl("icons/document.svg")
    }

    width: desk.cellW
    height: desk.cellH
    visible: placed
    x: (base ? base[0] : 0) + (carried ? desk.dragDx : 0)
    y: (base ? base[1] : 0) + (carried ? desk.dragDy : 0)
    z: carried ? 2 : selected ? 1 : 0
    // During a drag-and-drop the pointer carries a picture of the icon, so
    // the icon itself stays as a faint ghost where it would land.
    opacity: !placed ? 0 : carried && desk.dndActive ? 0.45 : 1
    onPlacedChanged: if (placed) Qt.callLater(() => icon.settled = true)

    // A slight overshoot on landing reads as the magnet catching the icon.
    Behavior on x {
        enabled: icon.settled && !icon.carried
        NumberAnimation { duration: Theme.animMs * 1.5; easing.type: Easing.OutBack; easing.overshoot: 0.9 }
    }
    Behavior on y {
        enabled: icon.settled && !icon.carried
        NumberAnimation { duration: Theme.animMs * 1.5; easing.type: Easing.OutBack; easing.overshoot: 0.9 }
    }
    Behavior on opacity { NumberAnimation { duration: Theme.animMs } }

    Item {
        id: lifted
        anchors.fill: parent
        scale: icon.carried ? 1.06 : 1
        opacity: icon.carried ? 0.9 : 1
        Behavior on scale { NumberAnimation { duration: Theme.animMs; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: Theme.animMs } }

        Rectangle {
            anchors.centerIn: picture
            width: desk.iconSize + 14
            height: desk.iconSize + 14
            radius: 10
            color: "#ffffff"
            opacity: into.containsDrag ? 0.35 : icon.selected ? 0.22 : mouse.containsMouse ? 0.1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.animMs } }
        }

        Image {
            id: picture
            anchors.horizontalCenter: parent.horizontalCenter
            y: 11
            width: desk.iconSize
            height: desk.iconSize
            source: icon.source
            sourceSize: Qt.size(desk.iconSize * 2, desk.iconSize * 2)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            smooth: true
            onStatusChanged: if (status === Image.Error && icon.modelData.thumb) icon.thumbFailed = true
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowOpacity: icon.carried ? 0.45 : 0.28
                shadowBlur: icon.carried ? 0.9 : 0.5
                shadowVerticalOffset: icon.carried ? 6 : 2
            }
        }

        Rectangle {
            visible: icon.selected && !icon.renaming
            anchors.centerIn: label
            width: label.paintedWidth + 10
            height: label.paintedHeight + 2
            radius: 5
            color: Theme.p.accent
        }

        Text {
            id: label
            visible: !icon.renaming
            anchors.horizontalCenter: parent.horizontalCenter
            y: picture.y + picture.height + 11
            width: parent.width - 10
            text: icon.modelData.label
            color: icon.selected ? Theme.p.onAccent : "#ffffff"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideRight
            lineHeight: 0.95
            font.pixelSize: Theme.smallFontSize + 1
            font.family: Theme.fontFamily
            font.weight: Font.DemiBold
            // White on a dark edge and shadow, as on macOS: legible on any
            // wallpaper without a plate behind the name.
            style: icon.selected ? Text.Normal : Text.Outline
            styleColor: "#59000000"
            layer.enabled: !icon.selected
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#000000"
                shadowOpacity: 0.85
                shadowBlur: 0.45
                shadowVerticalOffset: 1
            }
        }
    }

    Item {
        id: hit
        x: Math.round((parent.width - width) / 2)
        y: 2
        width: Math.max(desk.iconSize + 14, label.paintedWidth + 12)
        height: label.y + Math.max(label.paintedHeight, 14) + 2
    }

    MouseArea {
        id: mouse
        anchors.fill: hit
        hoverEnabled: true
        enabled: !icon.renaming
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        property point grab
        property bool moved: false
        property bool wasSelected: false

        function inField(m) { return mapToItem(icon.field, m.x, m.y) }

        onPressed: m => {
            icon.field.forceActiveFocus()
            const toggle = (m.modifiers & Qt.ControlModifier) !== 0
            wasSelected = icon.selected
            moved = false
            grab = inField(m)
            if (m.button === Qt.RightButton) {
                if (!icon.selected) desk.select(icon.name, false)
                desk.openMenu(grab.x, grab.y)
                return
            }
            desk.menuOpen = false
            if (toggle) desk.select(icon.name, true)
            else if (!icon.selected) desk.select(icon.name, false)
        }
        onPositionChanged: m => {
            if (!(pressedButtons & Qt.LeftButton) || !icon.selected) return
            const p = inField(m)
            const dx = p.x - grab.x, dy = p.y - grab.y
            if (!moved && Math.hypot(dx, dy) < 5) return
            moved = true
            desk.magnetHeld = (m.modifiers & Qt.ShiftModifier) !== 0
            if (!desk.dndActive && desk.beginDnd(icon.name, grab)) {
                dnd.hotSpot = Qt.point(Math.max(0, Math.min(desk.iconSize, m.x + hit.x - picture.x)),
                                       Math.max(0, Math.min(desk.iconSize, m.y + hit.y - picture.y)))
                Qt.callLater(() => dnd.Drag.active = true)   // runs until the drop
                return
            }
            if (!desk.dndActive) desk.drag(icon.name, dx, dy, desk.magnetHeld)
        }
        onReleased: m => {
            if (m.button !== Qt.LeftButton) return
            if (desk.dndActive) { moved = false; return }
            if (moved) {
                const p = inField(m)
                desk.drop(icon.name, p.x - grab.x, p.y - grab.y)
            } else if (wasSelected && !(m.modifiers & Qt.ControlModifier)) {
                desk.select(icon.name, false)
            }
            moved = false
        }
        onCanceled: { moved = false; if (!desk.dndActive) desk.cancelDrag() }
        onDoubleClicked: m => { if (m.button === Qt.LeftButton) desk.open([icon.name]) }
    }

    // A folder takes what is dropped on it and moves it inside (cut, not
    // copy), whether it comes from another desktop icon or from an app.
    DropArea {
        id: into
        anchors.fill: hit
        enabled: icon.modelData.kind === "dir" && !icon.renaming
        function paths(drag) {
            if (drag.source && drag.source.desktopDrag === true) {
                if (icon.selected) return []      // a folder never goes into itself
                return desk.selectedNames.map(n => desk.dir + "/" + n)
            }
            return drag.hasUrls ? desk.pathsOf(drag.urls) : []
        }
        onEntered: drag => {
            if (paths(drag).length === 0) { drag.accepted = false; return }
            drag.accept(Qt.MoveAction)
        }
        onPositionChanged: drag => desk.cancelDrag()
        onDropped: drop => {
            const list = paths(drop)
            if (list.length === 0) { drop.accepted = false; return }
            desk.moveInto(icon.name, list)
            drop.accept(Qt.MoveAction)
        }
    }

    // The drag-and-drop source: the selected files as file:// URIs, so a file
    // manager, browser, chat or editor window takes them as it takes files
    // dragged from its own kind of app.
    Item {
        id: dnd
        readonly property bool desktopDrag: true
        property point hotSpot: Qt.point(0, 0)
        visible: false
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction | Qt.LinkAction
        Drag.proposedAction: Qt.CopyAction
        Drag.imageSource: icon.source
        Drag.imageSourceSize: Qt.size(desk.iconSize, desk.iconSize)
        Drag.hotSpot: hotSpot
        Drag.source: dnd
        Drag.mimeData: {
            const names = desk.selectedNames.length ? desk.selectedNames : [icon.name]
            const uris = names.map(n => desk.uriOf(n))
            return {
                "text/uri-list": uris.join("\r\n") + "\r\n",
                "text/plain": names.map(n => desk.dir + "/" + n).join("\n")
            }
        }
        Drag.onDragFinished: desk.endDnd()
    }

    Rectangle {
        id: editor
        visible: icon.renaming
        anchors.horizontalCenter: parent.horizontalCenter
        y: label.y - 3
        width: Math.max(parent.width + 30, Math.min(260, input.contentWidth + 22))
        height: 24
        radius: 6
        color: Theme.p.surface
        border.width: 1
        border.color: desk.renameError !== "" ? Theme.p.red : Theme.p.accent

        onVisibleChanged: {
            if (!visible) return
            input.text = icon.name
            input.forceActiveFocus()
            const dot = icon.modelData.kind === "dir" ? -1 : icon.name.lastIndexOf(".")
            input.select(0, dot > 0 ? dot : icon.name.length)
        }

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 7
            anchors.rightMargin: 7
            verticalAlignment: TextInput.AlignVCenter
            horizontalAlignment: TextInput.AlignHCenter
            clip: true
            color: Theme.p.text
            selectionColor: Theme.p.accent
            selectedTextColor: Theme.p.onAccent
            font.pixelSize: Theme.smallFontSize + 1
            font.family: Theme.fontFamily
            onAccepted: desk.commitRename(text)
            onTextEdited: desk.renameError = ""
            Keys.onEscapePressed: { desk.renaming = ""; desk.renameError = ""; icon.field.forceActiveFocus() }
        }

        // Swallows clicks so the field behind doesn't take them as "outside".
        MouseArea { anchors.fill: parent; z: -1 }
    }

    Rectangle {
        visible: icon.renaming && desk.renameError !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: editor.bottom
        anchors.topMargin: 4
        width: problem.implicitWidth + 14
        height: problem.implicitHeight + 6
        radius: 5
        color: Theme.p.red

        Text {
            id: problem
            anchors.centerIn: parent
            text: desk.renameError
            color: "#ffffff"
            font.pixelSize: Theme.smallFontSize
            font.family: Theme.fontFamily
        }
    }
}
