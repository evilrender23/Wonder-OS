import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "../services"
import "../components"
import ".."

// Display settings, after macOS's: drag the monitors to arrange them (they
// snap edge to edge), pick resolution, refresh, scale and rotation per
// monitor, Identify flashes each one's number. The live layout comes from
// `wlr-randr --json` (the compositor's wlr-output-management); Apply writes
// [[outputs]] into config.toml through extras/scripts/wonderland-displays
// (validated, backed up) and the compositor hot-reloads it. Like every OS
// does for a mode change, it reverts on its own unless kept within 15 s.
Scope {
    id: root

    property bool open: false
    property var live: []          // outputs as wlr-randr reports them
    property var draft: []         // the edited copy
    property int sel: 0
    property string phase: "edit"  // edit | applying | confirm
    property int countdown: 0
    property string error: ""
    property bool identifying: false
    property bool dragging: false

    function toggle() { if (open) close(); else show() }
    function show() { open = true; phase = "edit"; error = ""; probe.running = true }
    function close() {
        if (phase === "confirm") revert()
        open = false
    }

    Connections {
        target: Popups
        function onDisplaysRequested() { root.show() }
        function onPanelRequested(id) { if (id === "displays") root.toggle() }
    }

    // ── model ───────────────────────────────────────────────────────────
    function rotated(t) { return t === "90" || t === "270" || t === "flipped-90" || t === "flipped-270" }
    function logical(d) {
        const w = rotated(d.transform) ? d.h : d.w, h = rotated(d.transform) ? d.w : d.h
        return { x: d.x, y: d.y, w: Math.round(w / d.scale), h: Math.round(h / d.scale) }
    }
    function resolutions(d) {
        const seen = {}, out = []
        for (const m of d.modes) {
            const k = `${m.width}x${m.height}`
            if (!seen[k]) { seen[k] = true; out.push({ w: m.width, h: m.height, native: m.width === d.nativeW && m.height === d.nativeH }) }
        }
        return out.sort((a, b) => b.w * b.h - a.w * a.h || b.w - a.w)
    }
    function refreshes(d) {
        const out = []
        for (const m of d.modes)
            if (m.width === d.w && m.height === d.h && !out.includes(Math.round(m.refresh))) out.push(Math.round(m.refresh))
        return out.sort((a, b) => b - a)
    }
    function fromLive(o, i) {
        const cur = o.modes.find(m => m.current) || o.modes[0] || { width: 1920, height: 1080, refresh: 60 }
        const pref = o.modes.find(m => m.preferred) || cur
        return {
            num: i + 1, name: o.name,
            label: [({ "Microstep": "MSI", "Goldstar Company Ltd": "LG", "Samsung Electric Company": "Samsung" })[o.make] || o.make, o.model]
                .filter(s => s).join(" ") || o.name,
            modes: o.modes, nativeW: pref.width, nativeH: pref.height,
            w: cur.width, h: cur.height, hz: Math.round(cur.refresh),
            x: o.position ? o.position.x : 0, y: o.position ? o.position.y : 0,
            scale: o.scale || 1, transform: o.transform || "normal"
        }
    }
    function payload(list) {
        return list.map(d => ({ name: d.name, mode: `${d.w}x${d.h}@${d.hz}`, position: [d.x, d.y],
                                scale: d.scale, transform: d.transform }))
    }
    readonly property bool dirty: JSON.stringify(payload(draft)) !== JSON.stringify(payload(live.map(fromLive)))

    function edit(fn) {
        const d = Object.assign({}, draft[sel])
        fn(d)
        const next = draft.slice()
        next[sel] = d
        draft = normalize(next)
    }
    function setResolution(w, h) {
        edit(d => {
            d.w = w; d.h = h
            const hz = refreshes(d)
            if (!hz.includes(d.hz)) d.hz = hz[0] || 60
        })
        // A size change can open a gap or an overlap: re-seat it next to the others.
        reseat(sel)
    }
    function normalize(list) {
        if (!list.length) return list
        const mx = Math.min(...list.map(d => d.x)), my = Math.min(...list.map(d => d.y))
        return list.map(d => Object.assign({}, d, { x: d.x - mx, y: d.y - my }))
    }

    // Nearest edge-to-edge spot beside another monitor, overlapping it along
    // the shared edge; aligns tops/bottoms (lefts/rights) when close.
    function snap(i, px, py) {
        const r = logical(draft[i])
        let best = null, bd = Infinity
        const near = (v, t) => Math.abs(v - t) < 48 ? t : v
        for (let j = 0; j < draft.length; j++) {
            if (j === i) continue
            const o = logical(draft[j])
            const clampY = y => Math.max(o.y - r.h + 1, Math.min(o.y + o.h - 1, near(near(y, o.y), o.y + o.h - r.h)))
            const clampX = x => Math.max(o.x - r.w + 1, Math.min(o.x + o.w - 1, near(near(x, o.x), o.x + o.w - r.w)))
            for (const c of [[o.x + o.w, clampY(py)], [o.x - r.w, clampY(py)],
                             [clampX(px), o.y + o.h], [clampX(px), o.y - r.h]]) {
                const dd = (c[0] - px) ** 2 + (c[1] - py) ** 2
                if (dd < bd) { bd = dd; best = c }
            }
        }
        return best ? best.map(Math.round) : [Math.round(px), Math.round(py)]
    }
    function moveTo(i, px, py) {
        const p = snap(i, px, py)
        const next = draft.slice()
        next[i] = Object.assign({}, next[i], { x: p[0], y: p[1] })
        draft = normalize(next)
    }
    function reseat(i) { const d = draft[i]; moveTo(i, d.x, d.y) }

    // ── processes ───────────────────────────────────────────────────────
    Process {
        id: probe
        command: ["wlr-randr", "--json"]
        stdout: StdioCollector {
            onStreamFinished: {
                let outs
                try { outs = JSON.parse(text) } catch (e) { root.error = I18n.tr("wlr-randr no devolvió datos"); return }
                root.live = outs.filter(o => o.enabled !== false)
                root.draft = root.live.map(root.fromLive)
                root.sel = Math.min(root.sel, Math.max(0, root.draft.length - 1))
            }
        }
        onExited: code => { if (code !== 0) root.error = I18n.tr("No se pudo leer la configuración (¿está wlr-randr instalado?)") }
    }
    Timer { id: reprobe; interval: 1500; onTriggered: probe.running = true }

    readonly property var scriptCmd: ["sh", "-c", 't="${XDG_CONFIG_HOME:-$HOME/.config}/driftwm/scripts/wonderland-displays"; [ -x "$t" ] || t=wonderland-displays; exec "$t" "$@"', "sh"]

    function apply() {
        error = ""
        phase = "applying"
        applier.command = scriptCmd.concat(["apply", JSON.stringify(payload(draft))])
        applier.running = true
    }
    function keep() { phase = "edit"; ticker.stop(); reprobe.restart() }
    function revert() {
        ticker.stop()
        phase = "edit"
        reverter.command = scriptCmd.concat(["revert"])
        reverter.running = true
    }

    Process {
        id: applier
        stderr: StdioCollector { id: applyErr }
        onExited: code => {
            if (code === 0) {
                root.phase = "confirm"
                root.countdown = 15
                ticker.restart()
                reprobe.restart()
            } else {
                root.phase = "edit"
                root.error = applyErr.text.trim().split("\n").slice(-3).join("\n") || I18n.tr("wonderland-displays no está instalado")
            }
        }
    }
    Process {
        id: reverter
        onExited: reprobe.restart()
    }
    Timer {
        id: ticker
        interval: 1000
        repeat: true
        onTriggered: { root.countdown -= 1; if (root.countdown <= 0) root.revert() }
    }
    Timer { id: unflash; interval: 2500; onTriggered: root.identifying = false }

    // ── identify ────────────────────────────────────────────────────────
    Variants {
        model: Quickshell.screens
        delegate: PanelWindow {
            required property var modelData
            screen: modelData
            readonly property var entry: root.draft.find(d => d.name === modelData.name)
            visible: root.identifying && !!entry
            WlrLayershell.namespace: "wonderland-identify"
            WlrLayershell.layer: WlrLayer.Overlay
            exclusionMode: ExclusionMode.Ignore
            color: "transparent"
            implicitWidth: 260
            implicitHeight: 170
            Rectangle {
                anchors.fill: parent
                radius: 18
                color: Theme.p.accent
                Column {
                    anchors.centerIn: parent
                    spacing: 4
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: entry ? entry.num : ""; color: Theme.p.onAccent; font.family: Theme.fontFamily; font.pixelSize: 72; font.weight: Font.DemiBold }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: entry ? `${entry.label} · ${entry.name}` : ""; color: Theme.p.onAccent; font.family: Theme.fontFamily; font.pixelSize: 13 }
                }
            }
        }
    }

    // ── window ──────────────────────────────────────────────────────────
    component Chip: Rectangle {
        id: chip
        property string text
        property bool checked: false
        signal clicked()
        implicitHeight: 28
        implicitWidth: chipText.implicitWidth + 22
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
        property bool primary: false
        property bool enabled_: true
        signal clicked()
        implicitHeight: 30
        implicitWidth: btnText.implicitWidth + 28
        radius: 7
        opacity: enabled_ ? 1 : 0.45
        color: primary ? (btnMouse.containsMouse ? Theme.p.accentHover : Theme.p.accent)
                       : (btnMouse.containsMouse ? Theme.p.hover : Theme.p.surface)
        border.width: primary ? 0 : 1
        border.color: Theme.p.stroke2
        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.text
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: btn.primary ? Theme.p.onAccent : Theme.p.text
        }
        MouseArea { id: btnMouse; anchors.fill: parent; hoverEnabled: true; onClicked: if (btn.enabled_) btn.clicked() }
    }

    component Field: RowLayout {
        property string label
        default property alias content: holder.data
        Layout.fillWidth: true
        spacing: 14
        Text {
            text: parent.label
            Layout.preferredWidth: 96
            font.family: Theme.fontFamily
            font.pixelSize: 13
            color: Theme.p.text2
        }
        Flow { id: holder; Layout.fillWidth: true; spacing: 6 }
    }

    PanelWindow {
        id: win
        visible: root.open
        screen: Quickshell.screens.find(s => {
            const outs = Compositor.state ? Compositor.state.outputs || [] : []
            const a = outs.find(o => o.active)
            return a && s.name === a.name
        }) || Quickshell.screens[0]
        WlrLayershell.namespace: "wonderland-displays"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
        exclusionMode: ExclusionMode.Ignore
        color: "transparent"
        implicitWidth: 760
        implicitHeight: body.implicitHeight + 36

        Rectangle {
            id: panel
            anchors.fill: parent
            radius: 14
            readonly property color base: Theme.p.elevated
            color: Theme.glassy(base, Theme.dark ? 0.90 : 0.94)
            border.width: 1
            border.color: Theme.p.stroke2
            focus: true
            Keys.onEscapePressed: root.close()

            ColumnLayout {
                id: body
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14

                // Title
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Icon { name: "monitor"; size: 18; color: Theme.p.accent }
                    Text { text: I18n.tr("Pantallas"); font.family: Theme.fontFamily; font.pixelSize: 17; font.weight: Font.DemiBold; color: Theme.p.text; Layout.fillWidth: true }
                    Rectangle {
                        width: 26; height: 26; radius: 13
                        color: closeMouse.containsMouse ? Theme.p.hover : "transparent"
                        Icon { anchors.centerIn: parent; name: "x"; size: 15; color: Theme.p.text2 }
                        MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.close() }
                    }
                }

                // Arrangement
                Rectangle {
                    id: stageBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: 230
                    radius: 10
                    color: Theme.p.sidebar
                    border.width: 1
                    border.color: Theme.p.stroke3
                    clip: true

                    // Frozen while dragging so the others don't rescale under the cursor.
                    property var frame: ({ k: 0.08, ox: 0, oy: 0, mx: 0, my: 0 })
                    function refit() {
                        if (root.dragging || !root.draft.length) return
                        const rs = root.draft.map(root.logical)
                        const mx = Math.min(...rs.map(r => r.x)), my = Math.min(...rs.map(r => r.y))
                        const bw = Math.max(...rs.map(r => r.x + r.w)) - mx, bh = Math.max(...rs.map(r => r.y + r.h)) - my
                        const k = Math.min((width - 80) / bw, (height - 60) / bh)
                        frame = { k: k, ox: (width - bw * k) / 2, oy: (height - bh * k) / 2, mx: mx, my: my }
                    }
                    Connections { target: root; function onDraftChanged() { stageBox.refit() } }
                    onWidthChanged: refit()
                    onHeightChanged: refit()

                    Text {
                        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 10
                        text: root.draft.length > 1 ? I18n.tr("Arrastra las pantallas para colocarlas como en tu mesa") : ""
                        font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.text3
                    }

                    Repeater {
                        model: root.draft
                        delegate: Rectangle {
                            id: mon
                            required property var modelData
                            required property int index
                            readonly property var r: root.logical(modelData)
                            readonly property real bx: stageBox.frame.ox + (r.x - stageBox.frame.mx) * stageBox.frame.k
                            readonly property real by: stageBox.frame.oy + (r.y - stageBox.frame.my) * stageBox.frame.k
                            x: bx
                            y: by
                            width: r.w * stageBox.frame.k
                            height: r.h * stageBox.frame.k
                            radius: 6
                            z: drag.drag.active ? 2 : 1
                            color: index === root.sel ? Theme.p.accentSoft : Theme.p.surface
                            border.width: index === root.sel ? 2 : 1
                            border.color: index === root.sel ? Theme.p.accent : Theme.p.stroke1

                            Column {
                                anchors.centerIn: parent
                                spacing: 2
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: mon.modelData.num; font.family: Theme.fontFamily; font.pixelSize: 26; font.weight: Font.DemiBold; color: mon.index === root.sel ? Theme.p.accent : Theme.p.text2 }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: mon.modelData.label; font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.text2; width: Math.min(implicitWidth, mon.width - 12); elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: `${mon.modelData.w} × ${mon.modelData.h} · ${mon.modelData.hz} Hz`; font.family: Theme.fontFamily; font.pixelSize: 10; color: Theme.p.text3 }
                            }

                            MouseArea {
                                id: drag
                                anchors.fill: parent
                                cursorShape: root.draft.length > 1 ? Qt.OpenHandCursor : Qt.ArrowCursor
                                drag.target: root.draft.length > 1 ? mon : null
                                drag.threshold: 3
                                onPressed: { root.sel = mon.index; root.dragging = true }
                                onReleased: {
                                    const f = stageBox.frame
                                    const lx = f.mx + (mon.x - f.ox) / f.k, ly = f.my + (mon.y - f.oy) / f.k
                                    const moved = Math.abs(mon.x - mon.bx) > 1 || Math.abs(mon.y - mon.by) > 1
                                    mon.x = Qt.binding(() => mon.bx)
                                    mon.y = Qt.binding(() => mon.by)
                                    root.dragging = false
                                    if (moved) root.moveTo(mon.index, lx, ly)
                                    else stageBox.refit()
                                }
                            }
                        }
                    }
                }

                // Selected monitor
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: root.draft.length > 0
                    readonly property var d: root.draft[root.sel] || null

                    Text {
                        text: parent.d ? `${parent.d.label}  ·  ${parent.d.name}` : ""
                        font.family: Theme.fontFamily; font.pixelSize: 14; font.weight: Font.DemiBold; color: Theme.p.text
                    }

                    Field {
                        label: I18n.tr("Resolución")
                        Repeater {
                            model: root.draft[root.sel] ? root.resolutions(root.draft[root.sel]).slice(0, 8) : []
                            delegate: Chip {
                                required property var modelData
                                readonly property var d: root.draft[root.sel]
                                text: `${modelData.w} × ${modelData.h}${modelData.native ? I18n.tr("  (nativa)") : ""}`
                                checked: !!d && d.w === modelData.w && d.h === modelData.h
                                onClicked: root.setResolution(modelData.w, modelData.h)
                            }
                        }
                    }
                    Field {
                        label: I18n.tr("Frecuencia")
                        Repeater {
                            model: root.draft[root.sel] ? root.refreshes(root.draft[root.sel]) : []
                            delegate: Chip {
                                required property var modelData
                                text: `${modelData} Hz`
                                checked: !!root.draft[root.sel] && (root.draft[root.sel].hz === modelData)
                                onClicked: root.edit(d => d.hz = modelData)
                            }
                        }
                    }
                    Field {
                        label: I18n.tr("Escala")
                        Repeater {
                            model: [1, 1.25, 1.5, 1.75, 2]
                            delegate: Chip {
                                required property var modelData
                                text: `${Math.round(modelData * 100)} %`
                                checked: !!root.draft[root.sel] && (Math.abs(root.draft[root.sel].scale - modelData) < 1e-6)
                                onClicked: { root.edit(d => d.scale = modelData); root.reseat(root.sel) }
                            }
                        }
                    }
                    Field {
                        label: I18n.tr("Rotación")
                        Repeater {
                            model: [["normal", "Normal"], ["90", "90°"], ["180", "180°"], ["270", "270°"]]
                            delegate: Chip {
                                required property var modelData
                                text: modelData[1]
                                checked: !!root.draft[root.sel] && (root.draft[root.sel].transform === modelData[0])
                                onClicked: { root.edit(d => d.transform = modelData[0]); root.reseat(root.sel) }
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.error !== ""
                    text: root.error
                    wrapMode: Text.Wrap
                    font.family: Theme.fontFamily; font.pixelSize: 12; color: Theme.p.red
                }

                // Keep-or-revert, as after any mode change.
                Rectangle {
                    Layout.fillWidth: true
                    visible: root.phase === "confirm"
                    implicitHeight: 48
                    radius: 9
                    color: Theme.p.accentSoft
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 10
                        Text {
                            Layout.fillWidth: true
                            text: I18n.tr("¿Mantener esta configuración? Se revertirá en %1 s.").arg(String(root.countdown))
                            font.family: Theme.fontFamily; font.pixelSize: 13; color: Theme.p.text
                        }
                        Button { text: I18n.tr("Revertir"); onClicked: root.revert() }
                        Button { text: I18n.tr("Mantener"); primary: true; onClicked: root.keep() }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.phase !== "confirm"
                    Button { text: I18n.tr("Identificar"); onClicked: { root.identifying = true; unflash.restart() } }
                    Item { Layout.fillWidth: true }
                    Button { text: I18n.tr("Restablecer"); enabled_: root.dirty; onClicked: root.draft = root.live.map(root.fromLive) }
                    Button { text: root.phase === "applying" ? I18n.tr("Aplicando…") : I18n.tr("Aplicar"); primary: true; enabled_: root.dirty && root.phase === "edit"; onClicked: root.apply() }
                }
            }
        }
    }

    IpcHandler {
        target: "displays"
        function toggle(): void { root.toggle() }
        function open(): void { root.show() }
    }
}
