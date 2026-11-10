import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "../services"
import "../components"
import ".."

// Wallpaper settings, after macOS's: a grid of live wallpapers on the left,
// the chosen one's settings on the right. A click on a wallpaper and every
// change to its settings are a preview: they reach the screen within a frame
// or two through the Wallpapers service, but nothing is kept until «Aplicar».
// «Descartar», Escape or closing the panel go back to what was applied.
// «Importar imagen» adds pictures of the user's own to the grid, ahead of the
// live ones; they have their own settings (fit, framing, brightness…) and an
// «Eliminar» button. A live wallpaper can be blended with one of them
// («Mezclar con imagen»): the picture is multiplied over the animation.
// Opened from Spotlight («wallpapers», «fondos») or
// `qs ipc call wallpapers toggle`.
Scope {
    id: root

    property bool open: false
    property string sel: ""
    property string filter: ""

    function toggle() { if (open) close(); else show() }
    function show() {
        Wallpapers.refresh()
        sel = Wallpapers.current || sel
        draft = ({})
        filter = ""
        open = true
    }
    function close() { discard(); open = false }

    Connections {
        target: Popups
        function onWallpapersRequested() { root.show() }
        function onPanelRequested(id) { if (id === "wallpapers") root.toggle() }
    }
    Connections {
        target: Wallpapers
        function onListChanged() {
            // Also when the selected one is gone (an imported picture that
            // was deleted).
            if ((!root.sel || !Wallpapers.byId(root.sel)) && Wallpapers.list.length)
                root.sel = Wallpapers.current || Wallpapers.list[0].id
        }
        // A picture just imported is selected and previewed, like a click on
        // its card; «Aplicar» keeps it.
        function onImported(ids) {
            if (!root.open || !ids.length) return
            if (root.importForBlend && root.selected && !root.selectedIsImage)
                root.set("blend_image", ids[0])
            else root.pick(ids[0])
        }
        function onImportingChanged() {
            // After `imported`, which is emitted right behind this change.
            if (!Wallpapers.importing) Qt.callLater(() => { root.importForBlend = false })
        }
    }

    readonly property var selected: Wallpapers.byId(sel)
    readonly property bool selectedIsImage: !!selected && selected.kind === "image"
    // The user's pictures, to blend a live wallpaper with.
    readonly property var pictures: Wallpapers.list.filter(w => w.kind === "image")
    // The picture the selected live wallpaper is blended with, "" for none
    // (also when the stored one was deleted since).
    readonly property string blendImage: {
        const id = vals.blend_image
        return id && id !== "none" && Wallpapers.byId(id) ? id : ""
    }
    // The file dialog was opened from «Mezclar con imagen»: what it brings is
    // blended with the selected wallpaper instead of replacing it.
    property bool importForBlend: false
    // Unapplied edits to the selected wallpaper's settings, over the stored
    // ones.
    property var draft: ({})
    readonly property var vals: selected ? Object.assign({}, Wallpapers.values(sel), draft) : ({})
    // What the panel shows differs from what is applied.
    readonly property bool dirty: {
        if (!selected) return false
        if (Wallpapers.current !== sel) return true
        const stored = Wallpapers.values(sel)
        return Object.keys(draft).some(k => draft[k] !== stored[k])
    }
    readonly property var shown: {
        const q = filter.trim().toLowerCase()
        const all = Wallpapers.list
        if (!q) return all
        return all.filter(w => [w.name, w.component, w.description, (w.tags || []).join(" ")]
                                .join(" ").toLowerCase().includes(q))
    }

    // Slider drags coalesce: the preview follows at most every 90 ms.
    function set(key, value) {
        if (!selected) return
        const d = Object.assign({}, draft)
        d[key] = value
        draft = d
        debounce.restart()
    }
    // Put on screen what the panel shows, or the applied wallpaper again
    // when there is no difference left.
    function preview() {
        debounce.stop()
        if (dirty) Wallpapers.preview(sel, draft)
        else Wallpapers.cancel()
    }
    Timer { id: debounce; interval: 90; onTriggered: root.preview() }

    function pick(id) {
        sel = id
        draft = ({})
        preview()
    }
    function resetDraft() {
        if (!selected) return
        const d = {}
        for (const p of selected.params) d[p.key] = p.default
        draft = d
        preview()
    }
    function apply() {
        debounce.stop()
        if (!dirty) return
        Wallpapers.apply(sel, draft)
        draft = ({})
    }
    function discard() {
        debounce.stop()
        draft = ({})
        if (Wallpapers.current) sel = Wallpapers.current
        Wallpapers.cancel()
    }
    // Ask for pictures to add. The panel is an overlay, so it steps aside
    // while the file dialog (an ordinary window) is open.
    function importImages(forBlend) {
        debounce.stop()
        importForBlend = !!forBlend
        Wallpapers.importImages()
    }
    function removeSelected() {
        if (!selectedIsImage) return
        debounce.stop()
        draft = ({})
        const id = sel
        sel = Wallpapers.current && Wallpapers.current !== id ? Wallpapers.current : ""
        Wallpapers.remove(id)
    }
    // Straight to the screen, for Spotlight-like callers (IPC, keybindings).
    function applyNow(id) {
        debounce.stop()
        sel = id
        draft = ({})
        Wallpapers.apply(id, {})
    }

    // A picture to blend with, in «Mezclar con imagen»; `picture` is null for
    // «Ninguna» and `add` makes it the tile that imports one.
    // Declared ahead of the other components: used as a Repeater delegate it
    // is "not a type" for Quickshell when it comes after them.
    component BlendTile: Rectangle {
        id: tile
        property var picture: null
        property bool add: false
        property bool checked: false
        signal clicked()
        width: 64; height: 36
        radius: 6
        color: Theme.p.surface
        border.width: checked ? 2 : 1
        border.color: checked ? Theme.p.accent : (tileMouse.containsMouse ? Theme.p.stroke1 : Theme.p.stroke3)
        clip: true
        Image {
            anchors.fill: parent
            anchors.margins: tile.checked ? 2 : 1
            visible: !!tile.picture
            source: tile.picture && tile.picture.preview ? "file://" + tile.picture.preview : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize: Qt.size(128, 72)
        }
        Icon {
            anchors.centerIn: parent
            visible: !tile.picture
            name: tile.add ? "plus" : "x"
            size: 14
            color: Theme.p.text3
        }
        MouseArea { id: tileMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: tile.clicked() }
    }

    // ── widgets ─────────────────────────────────────────────────────────
    component Chip: Rectangle {
        id: chip
        property string text
        property bool checked: false
        signal clicked()
        implicitHeight: 26
        implicitWidth: chipText.implicitWidth + 20
        radius: 7
        color: checked ? Theme.p.accent : chipMouse.containsMouse ? Theme.p.hover : Theme.p.surface
        border.width: checked ? 0 : 1
        border.color: Theme.p.stroke2
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.text
            font.family: Theme.fontFamily
            font.pixelSize: 12
            color: chip.checked ? Theme.p.onAccent : Theme.p.text
        }
        MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; onClicked: chip.clicked() }
    }

    component Button: Rectangle {
        id: btn
        property string text
        property string icon: ""
        property bool primary: false
        signal clicked()
        implicitHeight: 28
        implicitWidth: btnRow.implicitWidth + 22
        radius: 7
        opacity: enabled ? 1 : 0.45
        color: primary ? (btnMouse.containsMouse ? Theme.p.accentHover : Theme.p.accent)
                       : (btnMouse.containsMouse ? Theme.p.hover : Theme.p.surface)
        border.width: primary ? 0 : 1
        border.color: Theme.p.stroke2
        Row {
            id: btnRow
            anchors.centerIn: parent
            spacing: 6
            Icon { visible: btn.icon !== ""; name: btn.icon; size: 13; color: btn.primary ? Theme.p.onAccent : Theme.p.text2; anchors.verticalCenter: parent.verticalCenter }
            Text { text: btn.text; font.family: Theme.fontFamily; font.pixelSize: 12; color: btn.primary ? Theme.p.onAccent : Theme.p.text; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: btnMouse; anchors.fill: parent; hoverEnabled: true; onClicked: btn.clicked() }
    }

    // One setting, drawn from its schema entry.
    component Setting: ColumnLayout {
        id: setting
        required property var modelData
        readonly property var p: modelData
        readonly property var value: root.vals[p.key]
        Layout.fillWidth: true
        spacing: 5

        function fmt(v) {
            if (p.type === "int") return String(Math.round(v))
            const step = p.step || 0.01
            const digits = step >= 1 ? 0 : step >= 0.1 ? 1 : 2
            return Number(v).toFixed(digits)
        }

        RowLayout {
            Layout.fillWidth: true
            visible: setting.p.type !== "bool"
            Text {
                Layout.fillWidth: true
                text: setting.p.label
                elide: Text.ElideRight
                font.family: Theme.fontFamily; font.pixelSize: 12; color: Theme.p.text2
            }
            Text {
                visible: setting.p.type === "float" || setting.p.type === "int"
                text: setting.value !== undefined ? setting.fmt(setting.value) : ""
                font.family: Theme.monoFamily; font.pixelSize: 11; color: Theme.p.text3
            }
        }

        ThinSlider {
            id: slider
            visible: setting.p.type === "float" || setting.p.type === "int"
            Layout.fillWidth: true
            from: setting.p.min !== undefined ? setting.p.min : 0
            to: setting.p.max !== undefined ? setting.p.max : 1
            stepSize: setting.p.step || (setting.p.type === "int" ? 1 : 0.01)
            onMoved: root.set(setting.p.key, setting.p.type === "int" ? Math.round(value) : value)
            // Dragging breaks a plain binding; keep following the stored value.
            Binding on value {
                when: !slider.pressed
                value: typeof setting.value === "number" ? setting.value : 0
            }
        }

        Switch {
            visible: setting.p.type === "bool"
            text: setting.p.label
            detail: setting.p.description || ""
            checked: !!setting.value
            onToggled: v => root.set(setting.p.key, v)
        }

        Flow {
            visible: setting.p.type === "enum"
            Layout.fillWidth: true
            spacing: 5
            Repeater {
                model: setting.p.type === "enum" ? setting.p.options : []
                delegate: Chip {
                    required property var modelData
                    text: modelData.label
                    checked: setting.value === modelData.value
                    onClicked: root.set(setting.p.key, modelData.value)
                }
            }
        }

        ColorEditor {
            visible: setting.p.type === "color"
            Layout.fillWidth: true
            color: setting.p.type === "color" && setting.value ? setting.value : "#000000"
            onPicked: hex => root.set(setting.p.key, hex)
        }

        Text {
            visible: setting.p.type !== "bool" && !!setting.p.description
            Layout.fillWidth: true
            text: setting.p.description || ""
            wrapMode: Text.Wrap
            font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.text3
        }
    }

    // ── window ──────────────────────────────────────────────────────────
    PanelWindow {
        id: win
        visible: root.open && !Wallpapers.importing
        screen: Quickshell.screens.find(s => {
            const outs = Compositor.state ? Compositor.state.outputs || [] : []
            const a = outs.find(o => o.active)
            return a && s.name === a.name
        }) || Quickshell.screens[0]
        WlrLayershell.namespace: "wonderland-wallpapers"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
        exclusionMode: ExclusionMode.Ignore
        color: "transparent"
        implicitWidth: 1000
        implicitHeight: 640

        Rectangle {
            anchors.fill: parent
            radius: 14
            readonly property color base: Theme.p.elevated
            color: Qt.rgba(base.r, base.g, base.b, Theme.dark ? 0.92 : 0.95)
            border.width: 1
            border.color: Theme.p.stroke2
            focus: true
            Keys.onEscapePressed: root.close()

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Icon { name: "image"; size: 18; color: Theme.p.accent }
                    Text { text: I18n.tr("Fondos de pantalla"); font.family: Theme.fontFamily; font.pixelSize: 17; font.weight: Font.DemiBold; color: Theme.p.text }
                    Text {
                        text: {
                            if (!Wallpapers.loaded) return I18n.tr("Cargando…")
                            const own = Wallpapers.list.filter(w => w.kind === "image").length
                            const live = Wallpapers.list.length - own
                            return I18n.tr("%1 fondos animados").arg(String(live)) + (own ? " · " + (own === 1 ? I18n.tr("%1 imagen propia") : I18n.tr("%1 imágenes propias")).arg(String(own)) : "")
                        }
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily; font.pixelSize: 12; color: Theme.p.text3
                        Layout.fillWidth: true
                    }
                    Button { text: I18n.tr("Importar imagen"); icon: "plus"; onClicked: root.importImages() }
                    TextBox {
                        id: search
                        Layout.preferredWidth: 220
                        placeholder: I18n.tr("Buscar fondos")
                        onTextChanged: root.filter = text
                    }
                    Rectangle {
                        width: 26; height: 26; radius: 13
                        color: closeMouse.containsMouse ? Theme.p.hover : "transparent"
                        Icon { anchors.centerIn: parent; name: "x"; size: 15; color: Theme.p.text2 }
                        MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.close() }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 16

                    // Gallery
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 10
                        color: Theme.p.sidebar
                        border.width: 1
                        border.color: Theme.p.stroke3
                        clip: true

                        GridView {
                            id: grid
                            anchors.fill: parent
                            anchors.margins: 10
                            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 196)))
                            cellHeight: cellWidth * 0.5625 + 30
                            model: root.shown
                            boundsBehavior: Flickable.StopAtBounds
                            delegate: Item {
                                id: card
                                required property var modelData
                                readonly property bool isCurrent: Wallpapers.current === modelData.id
                                readonly property bool isSel: root.sel === modelData.id
                                width: grid.cellWidth
                                height: grid.cellHeight

                                Rectangle {
                                    id: thumb
                                    x: 5; y: 5
                                    width: parent.width - 10
                                    height: width * 0.5625
                                    radius: 8
                                    color: Theme.p.surface
                                    border.width: card.isSel ? 2 : 1
                                    border.color: card.isSel ? Theme.p.accent : (cardMouse.containsMouse ? Theme.p.stroke1 : Theme.p.stroke3)
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: card.isSel ? 2 : 1
                                        source: card.modelData.preview ? "file://" + card.modelData.preview : ""
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        sourceSize: Qt.size(480, 270)
                                    }
                                    Rectangle {
                                        visible: card.isCurrent
                                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6
                                        width: 20; height: 20; radius: 10
                                        color: Theme.p.accent
                                        Icon { anchors.centerIn: parent; name: "check"; size: 12; color: Theme.p.onAccent }
                                    }
                                    Rectangle {
                                        visible: card.isSel && !card.isCurrent && Wallpapers.previewing
                                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6
                                        width: 20; height: 20; radius: 10
                                        color: Qt.rgba(0, 0, 0, 0.55)
                                        Icon { anchors.centerIn: parent; name: "eye"; size: 12; color: "#ffffff" }
                                    }
                                    Rectangle {
                                        visible: !!card.modelData.heavy
                                        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 6
                                        width: 20; height: 20; radius: 10
                                        color: Qt.rgba(0, 0, 0, 0.55)
                                        Icon { anchors.centerIn: parent; name: "zap"; size: 11; color: Theme.p.yellow }
                                    }
                                    Rectangle {
                                        visible: card.modelData.kind === "image"
                                        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 6
                                        width: 20; height: 20; radius: 10
                                        color: Qt.rgba(0, 0, 0, 0.55)
                                        Icon { anchors.centerIn: parent; name: "image"; size: 11; color: "#ffffff" }
                                    }
                                }
                                Text {
                                    anchors.top: thumb.bottom; anchors.topMargin: 5
                                    anchors.left: thumb.left; anchors.right: thumb.right
                                    text: card.modelData.name
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                    font.family: Theme.fontFamily; font.pixelSize: 12
                                    font.weight: card.isCurrent ? Font.DemiBold : Font.Normal
                                    color: card.isSel ? Theme.p.accent : Theme.p.text
                                }
                                MouseArea {
                                    id: cardMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.pick(card.modelData.id)
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: Wallpapers.loaded && !root.shown.length
                            text: Wallpapers.error || I18n.tr("Ningún fondo coincide")
                            font.family: Theme.fontFamily; font.pixelSize: 13; color: Theme.p.text3
                        }
                    }

                    // Inspector
                    ColumnLayout {
                        Layout.preferredWidth: 300
                        Layout.maximumWidth: 300
                        Layout.fillHeight: true
                        spacing: 10
                        visible: !!root.selected

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: width * 0.5625
                            radius: 9
                            color: Theme.p.surface
                            clip: true
                            Image {
                                anchors.fill: parent
                                source: root.selected && root.selected.preview ? "file://" + root.selected.preview : ""
                                fillMode: Image.PreserveAspectCrop
                                sourceSize: Qt.size(480, 270)
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: root.selected ? root.selected.name : ""
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily; font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.p.text
                            }
                            // Only the user's own pictures can be deleted.
                            Button {
                                visible: root.selectedIsImage
                                text: I18n.tr("Eliminar")
                                icon: "trash-2"
                                onClicked: root.removeSelected()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.selected ? root.selected.description || "" : ""
                            wrapMode: Text.Wrap
                            font.family: Theme.fontFamily; font.pixelSize: 12; color: Theme.p.text2
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !!root.selected && !!root.selected.heavy
                            text: I18n.tr("Exigente con la GPU: baja los fotogramas por segundo si notas el equipo cargado.")
                            wrapMode: Text.Wrap
                            font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.yellow
                        }
                        Flickable {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentHeight: settingsCol.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            ColumnLayout {
                                id: settingsCol
                                width: parent.width - 6
                                spacing: 12

                                Repeater {
                                    model: root.selected ? root.selected.params.filter(p => p.group !== "general" && p.group !== "blend") : []
                                    delegate: Setting {}
                                }
                                // Blend with a picture: live wallpapers only.
                                Rectangle { visible: blendCol.visible; Layout.fillWidth: true; implicitHeight: 1; color: Theme.p.stroke3 }
                                ColumnLayout {
                                    id: blendCol
                                    visible: !!root.selected && !root.selectedIsImage
                                    Layout.fillWidth: true
                                    spacing: 12
                                    Text { text: I18n.tr("Mezclar con imagen"); font.family: Theme.fontFamily; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.p.text }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        BlendTile {
                                            checked: root.blendImage === ""
                                            onClicked: root.set("blend_image", "none")
                                        }
                                        Repeater {
                                            model: root.pictures
                                            delegate: BlendTile {
                                                required property var modelData
                                                picture: modelData
                                                checked: root.blendImage === modelData.id
                                                onClicked: root.set("blend_image", modelData.id)
                                            }
                                        }
                                        BlendTile { add: true; onClicked: root.importImages(true) }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: root.blendImage !== ""
                                            ? I18n.tr("La imagen se multiplica sobre el fondo animado, que la ilumina y la tiñe.")
                                            : I18n.tr("Elige una de tus imágenes, o añade una con +, para multiplicarla sobre el fondo animado.")
                                        wrapMode: Text.Wrap
                                        font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.text3
                                    }
                                    Repeater {
                                        model: root.selected && root.blendImage !== ""
                                            ? root.selected.params.filter(p => p.group === "blend" && p.key !== "blend_image") : []
                                        delegate: Setting {}
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.p.stroke3 }
                                Text { text: "General"; font.family: Theme.fontFamily; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.p.text }
                                Repeater {
                                    model: root.selected ? root.selected.params.filter(p => p.group === "general") : []
                                    delegate: Setting {}
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: !!root.selected && !!root.selected.notes
                                    text: root.selected && root.selected.notes ? I18n.tr("Nota: %1").arg(root.selected.notes) : ""
                                    wrapMode: Text.Wrap
                                    font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.text3
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: !!root.selected && !!root.selected.component
                                    text: I18n.tr("Basado en %1 de React Bits.").arg(root.selected ? root.selected.component : "")
                                    font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.text4
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: Wallpapers.error || Wallpapers.importError
                            wrapMode: Text.Wrap
                            font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.red
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Button { text: I18n.tr("Restablecer"); icon: "rotate-ccw"; onClicked: root.resetDraft() }
                            Item { Layout.fillWidth: true }
                            Button {
                                visible: Wallpapers.current !== ""
                                text: Wallpapers.current.startsWith("img-") ? I18n.tr("Quitar fondo") : I18n.tr("Quitar fondo animado")
                                onClicked: { debounce.stop(); root.draft = ({}); Wallpapers.restore() }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.p.stroke3 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Icon { visible: Wallpapers.previewing; name: "eye"; size: 13; color: Theme.p.accent }
                            Text {
                                Layout.fillWidth: true
                                text: Wallpapers.previewing ? I18n.tr("Vista previa")
                                    : root.dirty ? I18n.tr("Sin aplicar") : I18n.tr("Aplicado")
                                elide: Text.ElideRight
                                font.family: Theme.fontFamily; font.pixelSize: 11
                                color: Wallpapers.previewing ? Theme.p.accent : Theme.p.text3
                            }
                            Button {
                                visible: Wallpapers.previewing
                                text: I18n.tr("Descartar")
                                onClicked: root.discard()
                            }
                            Button {
                                primary: true
                                enabled: root.dirty
                                text: I18n.tr("Aplicar")
                                icon: "check"
                                onClicked: root.apply()
                            }
                        }
                    }
                }
            }
        }
    }

    IpcHandler {
        target: "wallpapers"
        function toggle(): void { root.toggle() }
        function open(): void { root.show() }
        function apply(id: string): void { root.applyNow(id) }
        function preview(id: string): void { root.show(); root.pick(id) }
        function keep(): void { root.apply() }
        function discard(): void { root.discard() }
        // «import»: open the panel and ask for pictures.
        function importImage(): void { root.show(); root.importImages() }
        function next(): void {
            const l = Wallpapers.list
            if (!l.length) return
            const i = l.findIndex(w => w.id === Wallpapers.current)
            root.applyNow(l[(i + 1) % l.length].id)
        }
    }
}
