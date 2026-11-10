import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "../services"
import "../components"
import ".."

// Screen capture, after macOS's ⇧⌘5: one overlay with a toolbar to pick what
// to grab — a window (hover lights it up, click takes it alone with its
// shadow), an area (drag) or one whole screen — as a picture or a recording.
// One click or one drag captures; there is no confirm step.
//
//   W / A / S    window / area / screen        R   picture ↔ recording
//   Space        window ↔ area                 T   timer: off, 3, 5, 10 s
//   Enter        the screen under the pointer  C   pointer in the picture
//   Esc or right click closes
//
// Afterwards a thumbnail sits in the corner for a few seconds: click opens
// the file; copy, show in folder and delete are on it. While recording, a
// small pill with the elapsed time stops it.
Scope {
    id: root

    property string pointerScreen: ""
    property string hint: ""

    function outputOf(name) {
        const outs = Compositor.state ? Compositor.state.outputs || [] : []
        return outs.find(o => o.name === name) || null
    }

    // Windows on `name`, topmost first, as rectangles in that screen's pixels.
    function windowRects(name, sw, sh) {
        const st = Compositor.state, out = outputOf(name), rects = []
        if (!st || !out) return rects
        for (const f of st.fullscreen || [])
            if (f.output === name) rects.push({ id: f.id, label: f.title || f.app_id, x: 0, y: 0, w: sw, h: sh })
        for (const p of st.pinned || [])
            if (p.output === name)
                rects.push({ id: p.id, label: p.title || p.app_id, w: p.size[0], h: p.size[1],
                    x: sw / 2 + p.position[0] - p.size[0] / 2, y: sh / 2 - p.position[1] - p.size[1] / 2 })
        const z = out.zoom, cx = out.camera[0], cy = out.camera[1]
        // `windows` is the stacking order bottom to top, with the focused
        // window promoted to the front.
        const wins = st.windows.filter(w => !w.is_widget && !w.minimized)
        const stack = wins.filter(w => w.is_focused).concat(wins.filter(w => !w.is_focused).reverse())
        for (const w of stack) {
            const ww = w.size[0] * z, wh = w.size[1] * z
            const x = (w.position[0] - cx) * z + sw / 2 - ww / 2
            const y = sh / 2 - (w.position[1] - cy) * z - wh / 2
            if (x < sw && y < sh && x + ww > 0 && y + wh > 0)
                rects.push({ id: w.id, label: w.title || w.app_id, x: x, y: y, w: ww, h: wh })
        }
        return rects
    }

    function clock(s) {
        const m = Math.floor(s / 60), r = s % 60
        return `${m < 10 ? "0" : ""}${m}:${r < 10 ? "0" : ""}${r}`
    }

    function screenNamed(name) {
        return Quickshell.screens.find(s => s.name === name) || Quickshell.screens[0] || null
    }

    Connections {
        target: Capture
        function onOpenChanged() {
            if (Capture.open) { root.pointerScreen = Capture.activeOutput(); card.shown = false }
        }
        function onFinished() {
            if (Capture.result && (Capture.result.error || Capture.result.kind !== "recording")) card.present()
        }
        function onJobChanged() { if (Capture.job) card.shown = false }
    }

    component ToolButton: Rectangle {
        id: btn
        property string icon: ""
        property string label: ""
        property string tip: ""
        property bool checked: false
        signal clicked()

        implicitHeight: 34
        implicitWidth: content.implicitWidth + (label === "" ? 18 : 22)
        radius: 8
        color: checked ? Theme.p.accent : area.containsMouse ? Theme.p.hover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animMs } }

        RowLayout {
            id: content
            anchors.centerIn: parent
            spacing: 6
            Icon {
                visible: btn.icon !== ""
                name: btn.icon
                size: 16
                color: btn.checked ? Theme.p.onAccent : Theme.p.text
            }
            Text {
                visible: btn.label !== ""
                text: btn.label
                color: btn.checked ? Theme.p.onAccent : Theme.p.text
                font.pixelSize: Theme.fontSize
                font.family: Theme.fontFamily
            }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()
            onContainsMouseChanged: root.hint = containsMouse ? btn.tip : (root.hint === btn.tip ? "" : root.hint)
        }
    }

    component Separator: Rectangle {
        implicitWidth: 1
        implicitHeight: 22
        color: Theme.p.stroke2
        Layout.leftMargin: 4
        Layout.rightMargin: 4
    }

    Variants {
        model: Quickshell.screens

        PanelWindow {
            id: win

            required property var modelData
            readonly property string name: modelData.name
            readonly property bool here: root.pointerScreen === name
            readonly property var rects: Capture.open && Capture.target === "window"
                ? root.windowRects(name, width, height) : []

            property real mx: -1
            property real my: -1
            property bool dragging: false
            property real ax: 0
            property real ay: 0

            readonly property var hovered: {
                if (!here || mx < 0 || Capture.target !== "window") return null
                return rects.find(r => mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) || null
            }
            // What a click would take, clipped to this screen; null = nothing.
            readonly property var hole: {
                if (!Capture.open || !here) return null
                if (Capture.target === "screen") return { x: 0, y: 0, w: width, h: height }
                if (Capture.target === "window") return hovered ? clip(hovered) : null
                if (!dragging) return null
                return clip({ x: Math.min(ax, mx), y: Math.min(ay, my), w: Math.abs(mx - ax), h: Math.abs(my - ay) })
            }

            function clip(r) {
                const x = Math.max(0, Math.round(r.x)), y = Math.max(0, Math.round(r.y))
                const x2 = Math.min(width, Math.round(r.x + r.w)), y2 = Math.min(height, Math.round(r.y + r.h))
                return { x: x, y: y, w: Math.max(0, x2 - x), h: Math.max(0, y2 - y) }
            }
            function layout(r) { return `${modelData.x + r.x},${modelData.y + r.y} ${r.w}x${r.h}` }

            function take() {
                if (Capture.target === "screen") Capture.pick("screen", name, name, { x: 0, y: 0, w: width, h: height })
                else if (Capture.target === "window" && hovered) {
                    const r = clip(hovered)
                    if (Capture.record) Capture.pick("area", layout(r), name, r)
                    else Capture.pick("window", hovered.id, name, r)
                }
            }

            screen: modelData
            visible: Capture.open
            WlrLayershell.namespace: "wonderland-capture"
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
            exclusionMode: ExclusionMode.Ignore
            anchors { top: true; bottom: true; left: true; right: true }
            color: "transparent"

            onVisibleChanged: {
                dragging = false
                mx = -1
                if (visible) keys.forceActiveFocus()
            }

            Item {
                id: keys
                anchors.fill: parent
                focus: true
                Keys.onPressed: event => {
                    event.accepted = true
                    switch (event.key) {
                    case Qt.Key_Escape: win.dragging ? win.dragging = false : Capture.close(); break
                    case Qt.Key_W: case Qt.Key_1: Capture.target = "window"; break
                    case Qt.Key_A: case Qt.Key_2: Capture.target = "area"; break
                    case Qt.Key_S: case Qt.Key_3: Capture.target = "screen"; break
                    case Qt.Key_Space: Capture.target = Capture.target === "window" ? "area" : "window"; break
                    case Qt.Key_R: Capture.record = !Capture.record; break
                    case Qt.Key_T: Capture.cycleDelay(); break
                    case Qt.Key_C: Capture.opts.cursor = !Capture.opts.cursor; break
                    case Qt.Key_Return: case Qt.Key_Enter: {
                        const n = root.pointerScreen || win.name
                        const s = root.screenNamed(n)
                        Capture.pick("screen", n, n, { x: 0, y: 0, w: s.width, h: s.height })
                        break
                    }
                    default: event.accepted = false
                    }
                }
            }

            // The veil, with a hole over what would be captured.
            Repeater {
                model: 4
                Rectangle {
                    required property int index
                    readonly property var h: win.hole || { x: 0, y: 0, w: win.width, h: 0 }
                    color: Qt.rgba(0, 0, 0, Capture.target === "area" && !win.dragging ? 0.18 : 0.32)
                    x: index === 3 ? h.x + h.w : 0
                    y: index === 0 ? 0 : index === 1 ? h.y + h.h : h.y
                    width: index < 2 ? win.width : index === 2 ? h.x : win.width - h.x - h.w
                    height: index === 0 ? h.y : index === 1 ? win.height - h.y - h.h : h.h
                }
            }

            Rectangle {
                visible: win.hole !== null && win.hole.w > 0
                x: win.hole ? win.hole.x : 0
                y: win.hole ? win.hole.y : 0
                width: win.hole ? win.hole.w : 0
                height: win.hole ? win.hole.h : 0
                color: Capture.target === "window" ? Qt.rgba(Theme.p.accent.r, Theme.p.accent.g, Theme.p.accent.b, 0.16) : "transparent"
                border.width: 2
                border.color: Capture.record ? Theme.p.red : Theme.p.accent
            }

            // Crosshair, so an area starts exactly where intended.
            Rectangle {
                visible: Capture.target === "area" && win.here && !win.dragging && win.mx >= 0 && !bar.containsPointer
                x: Math.round(win.mx); y: 0; width: 1; height: win.height
                color: Qt.rgba(1, 1, 1, 0.55)
            }
            Rectangle {
                visible: Capture.target === "area" && win.here && !win.dragging && win.mx >= 0 && !bar.containsPointer
                x: 0; y: Math.round(win.my); width: win.width; height: 1
                color: Qt.rgba(1, 1, 1, 0.55)
            }

            Rectangle {
                id: chip
                readonly property var h: win.hole
                readonly property string label: !h ? ""
                    : Capture.target === "window" ? (win.hovered ? win.hovered.label : "")
                    : Capture.target === "screen" ? `${win.name} · ${win.width} × ${win.height}`
                    : `${h.w} × ${h.h}`
                visible: label !== ""
                width: Math.min(chipText.implicitWidth + 16, 420)
                height: 24
                radius: 6
                color: Qt.rgba(0, 0, 0, 0.72)
                x: h ? Math.max(8, Math.min(win.width - width - 8, h.x + (h.w - width) / 2)) : 0
                y: !h ? 0 : Capture.target === "area" ? (h.y + h.h + 8 + height < win.height ? h.y + h.h + 8 : Math.max(8, h.y - height - 8))
                    : Math.max(Theme.barHeight + 12, h.y + 12)
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, 404)
                    elide: Text.ElideRight
                    text: chip.label
                    color: "#ffffff"
                    font.pixelSize: Theme.smallFontSize
                    font.family: Theme.fontFamily
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Capture.target === "area" ? Qt.CrossCursor : Qt.PointingHandCursor
                onPositionChanged: mouse => {
                    root.pointerScreen = win.name
                    win.mx = Math.max(0, Math.min(win.width, mouse.x))
                    win.my = Math.max(0, Math.min(win.height, mouse.y))
                }
                onPressed: mouse => {
                    root.pointerScreen = win.name
                    win.mx = mouse.x
                    win.my = mouse.y
                    if (mouse.button === Qt.RightButton) {
                        if (win.dragging) win.dragging = false
                        else Capture.close()
                    } else if (Capture.target === "area") {
                        win.ax = mouse.x
                        win.ay = mouse.y
                        win.dragging = true
                    }
                }
                onReleased: mouse => {
                    if (mouse.button !== Qt.LeftButton) return
                    if (Capture.target !== "area") { win.take(); return }
                    if (!win.dragging) return
                    const r = win.hole
                    win.dragging = false
                    // A stray click is not a selection.
                    if (r && r.w >= 6 && r.h >= 6) Capture.pick("area", win.layout(r), win.name, r)
                }
            }

            Rectangle {
                id: bar
                readonly property bool containsPointer: barHover.hovered
                visible: win.here && !win.dragging
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Math.round(win.height * 0.11)
                width: tools.implicitWidth + 16
                height: tools.implicitHeight + 16 + hintText.height + 6
                radius: 14
                readonly property color base: Theme.p.elevated
                color: Qt.rgba(base.r, base.g, base.b, 0.96)
                border.width: 1
                border.color: Theme.p.stroke2

                HoverHandler { id: barHover }
                // Clicks on the toolbar's padding must not start an area.
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

                RowLayout {
                    id: tools
                    x: 8
                    y: 8
                    spacing: 2

                    ToolButton { icon: "x"; tip: "Cerrar (Esc)"; onClicked: Capture.close() }
                    Separator {}
                    ToolButton {
                        icon: "app-window"; label: "Ventana"; checked: Capture.target === "window"
                        tip: "Clic en una ventana para capturarla sola, con su sombra (W)"
                        onClicked: Capture.target = "window"
                    }
                    ToolButton {
                        icon: "square-dashed"; label: "Área"; checked: Capture.target === "area"
                        tip: "Arrastra para elegir un área (A)"
                        onClicked: Capture.target = "area"
                    }
                    ToolButton {
                        icon: "monitor"; label: "Pantalla"; checked: Capture.target === "screen"
                        tip: "Clic en una pantalla para capturarla entera (S)"
                        onClicked: Capture.target = "screen"
                    }
                    Separator {}
                    ToolButton {
                        icon: "camera"; checked: !Capture.record
                        tip: "Captura de imagen (R alterna)"
                        onClicked: Capture.record = false
                    }
                    ToolButton {
                        icon: "video"; checked: Capture.record
                        tip: Capture.recording ? "Ya hay una grabación en curso" : "Grabar vídeo (R alterna)"
                        onClicked: Capture.record = true
                    }
                    Separator {}
                    ToolButton {
                        icon: "timer"; label: Capture.opts.delay > 0 ? `${Capture.opts.delay} s` : ""
                        checked: Capture.opts.delay > 0
                        tip: "Temporizador: sin espera, 3, 5 o 10 s (T)"
                        onClicked: Capture.cycleDelay()
                    }
                    ToolButton {
                        visible: !Capture.record
                        icon: "mouse-pointer-2"; checked: Capture.opts.cursor
                        tip: "Incluir el puntero en la imagen (C)"
                        onClicked: Capture.opts.cursor = !Capture.opts.cursor
                    }
                    ToolButton {
                        visible: !Capture.record
                        icon: Capture.opts.save ? "folder-open" : "clipboard"
                        tip: Capture.opts.save ? "Se guarda en Imágenes/Capturas y se copia al portapapeles"
                            : "Solo al portapapeles, sin archivo"
                        onClicked: Capture.opts.save = !Capture.opts.save
                    }
                    ToolButton {
                        visible: Capture.record
                        icon: Capture.opts.audio ? "volume-2" : "volume-x"; checked: Capture.opts.audio
                        tip: Capture.opts.audio ? "Graba el sonido del sistema" : "Vídeo sin sonido"
                        onClicked: Capture.opts.audio = !Capture.opts.audio
                    }
                }

                Text {
                    id: hintText
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    width: parent.width - 24
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: root.hint !== "" ? root.hint
                        : Capture.record && Capture.recording ? "Ya hay una grabación en curso: detenla antes de empezar otra"
                        : Capture.target === "window" ? (Capture.record ? "Clic en una ventana para grabarla" : "Clic en una ventana para capturarla")
                        : Capture.target === "area" ? (Capture.record ? "Arrastra el área que quieres grabar" : "Arrastra el área que quieres capturar")
                        : (Capture.record ? "Clic en la pantalla que quieres grabar" : "Clic en la pantalla que quieres capturar")
                    color: Theme.p.text3
                    font.pixelSize: Theme.smallFontSize
                    font.family: Theme.fontFamily
                }
            }
        }
    }

    // Countdown before a timed capture; a click on it cancels.
    PanelWindow {
        screen: Capture.job ? root.screenNamed(Capture.job.screen) : null
        visible: Capture.job !== null && Capture.countdown > 0
        WlrLayershell.namespace: "wonderland-capture-countdown"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
        exclusionMode: ExclusionMode.Ignore
        anchors.bottom: true
        margins.bottom: 120
        color: "transparent"
        implicitWidth: countRow.implicitWidth + 28
        implicitHeight: 44

        Rectangle {
            anchors.fill: parent
            radius: 22
            color: Qt.rgba(0, 0, 0, 0.78)
            RowLayout {
                id: countRow
                anchors.centerIn: parent
                spacing: 10
                Text {
                    text: Capture.countdown
                    color: "#ffffff"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    font.family: Theme.fontFamily
                }
                Text {
                    text: "Clic para cancelar"
                    color: Qt.rgba(1, 1, 1, 0.7)
                    font.pixelSize: Theme.smallFontSize
                    font.family: Theme.fontFamily
                }
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Capture.cancel() }
        }
    }

    // The recording pill: elapsed time and stop. It stays out of the recorded
    // rectangle when there is room, so it does not end up in the video.
    PanelWindow {
        id: pill

        readonly property var region: Capture.recRegion
        readonly property var others: Quickshell.screens.filter(s => !region || s.name !== region.screen)
        readonly property var home: region ? root.screenNamed(region.screen) : null
        readonly property bool whole: region !== null && home !== null && region.w >= home.width && region.h >= home.height
        // A whole-screen recording moves the pill to another monitor.
        readonly property var spot: whole && others.length > 0 ? others[0] : (home || Quickshell.screens[0] || null)
        readonly property bool sameScreen: region !== null && spot !== null && spot.name === region.screen
        readonly property int edge: 12
        readonly property int topEdge: Theme.barHeight + 10
        readonly property bool hitsTopRight: sameScreen && !whole
            && region.x + region.w > spot.width - implicitWidth - edge && region.y < topEdge + implicitHeight
        readonly property bool hitsTopLeft: sameScreen && !whole
            && region.x < implicitWidth + edge && region.y < topEdge + implicitHeight

        screen: spot
        visible: Capture.recording
        WlrLayershell.namespace: "wonderland-capture-recording"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
        exclusionMode: ExclusionMode.Ignore
        anchors.top: !(hitsTopRight && hitsTopLeft)
        anchors.bottom: hitsTopRight && hitsTopLeft
        anchors.right: !hitsTopRight || hitsTopLeft
        anchors.left: hitsTopRight && !hitsTopLeft
        margins { top: topEdge; bottom: edge; left: edge; right: edge }
        color: "transparent"
        implicitWidth: pillRow.implicitWidth + 22
        implicitHeight: 30

        Rectangle {
            anchors.fill: parent
            radius: 15
            color: pillMouse.containsMouse ? Qt.rgba(0.16, 0.05, 0.05, 0.92) : Qt.rgba(0, 0, 0, 0.82)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.14)

            RowLayout {
                id: pillRow
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    implicitWidth: 9; implicitHeight: 9; radius: 4.5
                    color: "#ff453a"
                    SequentialAnimation on opacity {
                        running: Capture.recording
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 700 }
                        NumberAnimation { to: 1; duration: 700 }
                    }
                }
                Text {
                    text: root.clock(Capture.recSeconds)
                    color: "#ffffff"
                    font.pixelSize: Theme.fontSize
                    font.family: Theme.monoFamily
                }
                Text {
                    text: "Detener"
                    color: Qt.rgba(1, 1, 1, 0.75)
                    font.pixelSize: Theme.smallFontSize
                    font.family: Theme.fontFamily
                }
            }
            MouseArea {
                id: pillMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: Capture.stop()
            }
        }
    }

    // The result, in the corner for a few seconds.
    PanelWindow {
        id: card

        property bool shown: false
        readonly property var res: Capture.result
        readonly property bool failed: res !== null && res.error !== undefined
        readonly property bool isShot: res !== null && res.kind === "shot"

        function present() { shown = true; linger.restart() }

        screen: root.screenNamed(Capture.resultScreen)
        visible: shown && res !== null
        WlrLayershell.namespace: "wonderland-capture-result"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
        exclusionMode: ExclusionMode.Ignore
        anchors { bottom: true; right: true }
        margins { bottom: 24; right: 24 }
        color: "transparent"
        implicitWidth: 264
        implicitHeight: body.implicitHeight + 16

        Timer {
            id: linger
            interval: card.failed ? 7000 : 6000
            onTriggered: if (cardHover.hovered) restart(); else card.shown = false
        }

        Rectangle {
            anchors.fill: parent
            radius: 12
            readonly property color base: Theme.p.elevated
            color: Qt.rgba(base.r, base.g, base.b, 0.97)
            border.width: 1
            border.color: card.failed ? Theme.p.red : Theme.p.stroke2

            HoverHandler { id: cardHover }

            ColumnLayout {
                id: body
                x: 8
                y: 8
                width: parent.width - 16
                spacing: 8

                Rectangle {
                    visible: !card.failed
                    Layout.fillWidth: true
                    implicitHeight: 140
                    radius: 8
                    color: Theme.p.track
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 4
                        visible: card.isShot
                        source: card.isShot ? "file://" + encodeURI(card.res.path) : ""
                        sourceSize.width: 512
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                    }
                    Icon { anchors.centerIn: parent; visible: !card.isShot; name: "film"; size: 40; color: Theme.p.text3 }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { Capture.openFile(card.res.path); card.shown = false }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    text: !card.res ? ""
                        : card.failed ? card.res.error
                        : card.isShot ? `${card.res.width} × ${card.res.height} · ${card.res.saved ? "guardada" : "solo portapapeles"}${card.res.copied && card.res.saved ? " y copiada" : ""}`
                        : `Grabación de ${root.clock(Math.round(card.res.seconds))} · ${card.res.bytes < 1048576 ? Math.max(1, Math.round(card.res.bytes / 1024)) + " KB" : (card.res.bytes / 1048576).toFixed(1) + " MB"}`
                    color: card.failed ? Theme.p.red : Theme.p.text2
                    font.pixelSize: Theme.smallFontSize
                    font.family: Theme.fontFamily
                }

                RowLayout {
                    visible: !card.failed
                    Layout.fillWidth: true
                    spacing: 2
                    ToolButton { icon: "copy"; label: "Copiar"; onClicked: { Capture.copy(card.res); card.shown = false } }
                    ToolButton {
                        visible: card.res !== null && card.res.saved !== false
                        icon: "folder-open"; label: "Carpeta"
                        onClicked: { Capture.reveal(card.res.path); card.shown = false }
                    }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        visible: card.res !== null && card.res.saved !== false
                        icon: "trash-2"
                        onClicked: { card.shown = false; Capture.discard(card.res) }
                    }
                }
            }
        }
    }

    IpcHandler {
        target: "capture"

        function toggle(): void { Capture.toggle() }
        // mode: window | area | screen | record | "last" (whatever was used last)
        function open(mode: string): void { Capture.show(mode) }
        function close(): void { Capture.close() }
        // Capture right now, no overlay: `screen` (the active one) or `window` (the focused one).
        function shot(what: string): void { Capture.instant(what === "window" ? "window" : "screen") }
        // Start choosing what to record, or stop the recording in progress.
        function record(): void { Capture.toggleRecord() }
        function stop(): void { Capture.stop() }
        // What a click in the overlay does: kind screen|window|area, value an
        // output name, a window id or "X,Y WxH" in layout pixels.
        function pick(kind: string, value: string, screen: string): void { Capture.pick(kind, value, screen, null) }
        function status(): string {
            return JSON.stringify({ open: Capture.open, target: Capture.target, record: Capture.record,
                recording: Capture.recording, seconds: Capture.recSeconds, result: Capture.result })
        }
    }
}
