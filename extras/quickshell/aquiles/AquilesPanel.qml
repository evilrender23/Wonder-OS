import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "../services"
import "../components"
import ".."

// AquilesAI: the agent's side sheet on the active monitor. Chat with live
// reasoning, tool calls and permission prompts; the session list; and the
// engine page (Hermes providers and their API keys, or a coding CLI).
// Enter sends, Shift+Enter breaks the line, Esc closes, "/" lists commands.
//
// Its own layer surface, like Spotlight: exclusive keyboard focus while it is
// a transient sheet, on-demand once pinned so it can stay beside your windows.
Scope {
    id: root

    property bool open: false
    property var screen: null
    property string view: "chat"        // chat | sessions | models | engine
    readonly property bool pinned: Settings.data.aquilesPinned

    // Open on another monitor, the shortcut brings the sheet to the focused one.
    function toggle() { if (open && screen === activeScreen()) close(); else show() }
    function show() {
        screen = activeScreen()
        open = true
        if (Aquiles.isHermes) Aquiles.connect()
        Aquiles.probeBins()
    }
    function close() { open = false }
    function ask(text) { show(); view = "chat"; Aquiles.send(text) }

    // The model manager. A query that names exactly one model picks it outright.
    function pickModel(query) {
        query = String(query || "").trim()
        if (Aquiles.isHermes && !Aquiles.providers.length) Aquiles.refreshProviders()
        const hits = query ? Aquiles.modelChoices(query) : []
        const exact = hits.filter(m => m.id.toLowerCase() === query.toLowerCase())
        if (query && (hits.length === 1 || exact.length === 1)) { const m = exact[0] || hits[0]; Aquiles.setModel(m.id, m.provider); view = "chat"; return }
        modelFilter.text = query
        view = "models"
        modelFilter.focusInput()
    }

    function activeScreen() {
        const outs = Compositor.state ? Compositor.state.outputs || [] : []
        const active = outs.find(o => o.active)
        if (active) {
            const s = Quickshell.screens.find(s => s.name === active.name)
            if (s) return s
        }
        return Quickshell.screens[0]
    }

    Connections {
        target: Popups
        function onAquilesRequested() { root.toggle() }
        function onPanelRequested(id) { if (id === "aquiles") root.toggle() }
    }

    function ago(ts) {
        if (!ts) return ""
        const s = Date.now() / 1000 - (ts > 1e12 ? ts / 1000 : ts)
        if (s < 90) return "ahora"
        if (s < 3600) return Math.round(s / 60) + " min"
        if (s < 86400) return Math.round(s / 3600) + " h"
        return Math.round(s / 86400) + " d"
    }

    component Section: Label {
        role: "section"
        Layout.topMargin: 10
        Layout.fillWidth: true
    }

    PanelWindow {
        id: win
        screen: root.screen
        visible: root.open
        WlrLayershell.namespace: "wonderland-aquiles"
        WlrLayershell.layer: WlrLayer.Top
        WlrLayershell.keyboardFocus: root.pinned ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.Exclusive
        exclusionMode: ExclusionMode.Ignore
        // Left edge: under the ☤ mark that opens it.
        anchors { top: true; left: true; bottom: true }
        margins { top: Theme.barHeight + 8; left: 8; bottom: 8 }
        color: "transparent"
        implicitWidth: 500

        onVisibleChanged: if (visible) { input.forceActiveFocus(); intro.restart() }

        Rectangle {
            id: card
            anchors.fill: parent
            radius: 14
            readonly property color base: Theme.p.elevated
            color: base
            border.width: 1
            border.color: Theme.p.stroke2
            clip: true

            NumberAnimation { id: intro; target: card; property: "opacity"; from: 0; to: 1; duration: Theme.animMs }

            Keys.onEscapePressed: root.close()

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                // ── header ──────────────────────────────────────────────
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Icon { name: "sparkles"; size: 18; color: Theme.p.accent }
                    Label { text: "AquilesAI"; role: "title" }
                    Rectangle {
                        radius: 9
                        implicitHeight: 18
                        implicitWidth: engineTag.implicitWidth + 14
                        color: Theme.p.accentSoft
                        Label { id: engineTag; anchors.centerIn: parent; role: "caption"; color: Theme.p.accent; text: Aquiles.engineName(Aquiles.backend) }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.view = "engine" }
                    }
                    Rectangle {
                        visible: Aquiles.isHermes
                        implicitWidth: 7; implicitHeight: 7; radius: 4
                        color: Aquiles.link === "open" ? Theme.p.green : Aquiles.link === "error" ? Theme.p.red : Theme.p.yellow
                    }
                    Item { Layout.fillWidth: true }
                    IconButton { icon: "plus"; size: 26; tooltip: "Chat nuevo"; onClicked: { Aquiles.newChat(); root.view = "chat"; input.forceActiveFocus() } }
                    IconButton { icon: "clock"; size: 26; tooltip: "Sesiones"; checked: root.view === "sessions"
                        onClicked: { root.view = root.view === "sessions" ? "chat" : "sessions"; if (Aquiles.isHermes) Aquiles.refreshSessions() } }
                    IconButton { icon: "cpu"; size: 26; tooltip: "Modelos"; checked: root.view === "models"
                        onClicked: { if (root.view === "models") root.view = "chat"; else root.pickModel("") } }
                    IconButton { icon: "sliders-horizontal"; size: 26; tooltip: "Motor, permisos y API keys"; checked: root.view === "engine"
                        onClicked: root.view = root.view === "engine" ? "chat" : "engine" }
                    IconButton { icon: "pin"; size: 26; tooltip: root.pinned ? "Soltar" : "Fijar junto a las ventanas"; checked: root.pinned
                        onClicked: Settings.data.aquilesPinned = !root.pinned }
                    IconButton { icon: "x"; size: 26; tooltip: "Cerrar (Esc)"; onClicked: root.close() }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.p.stroke3 }

                // ── chat ────────────────────────────────────────────────
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.view === "chat"

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width - 60
                        visible: Aquiles.messages.count === 0
                        spacing: 6
                        Icon { name: "sparkles"; size: 34; color: Theme.p.stroke1; Layout.alignment: Qt.AlignHCenter }
                        Label { text: "¿En qué te ayudo?"; role: "title"; Layout.alignment: Qt.AlignHCenter }
                        Label {
                            Layout.fillWidth: true
                            role: "muted"
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            text: Aquiles.isHermes
                                ? (Aquiles.link === "error" ? Aquiles.linkError
                                    : Aquiles.link !== "open" ? "Conectando con Hermes…"
                                    : `${Aquiles.model || "modelo por defecto"} · ${Aquiles.provider}\nEscribe / para ver los comandos.`)
                                : (Aquiles.bins[Aquiles.backend] ? `${Aquiles.engineName(Aquiles.backend)} en ${Aquiles.cwd}`
                                    : Aquiles.binsReady ? `«${Aquiles.backend}» no está instalado.` : "")
                        }
                    }

                    ListView {
                        id: chat
                        anchors.fill: parent
                        clip: true
                        spacing: 8
                        model: Aquiles.messages
                        boundsBehavior: Flickable.StopAtBounds
                        property bool stick: true
                        onMovementEnded: stick = atYEnd
                        onContentHeightChanged: if (stick) Qt.callLater(positionViewAtEnd)
                        onCountChanged: { stick = true; Qt.callLater(positionViewAtEnd) }

                        delegate: Item {
                            id: msg
                            required property int index
                            required property string kind
                            required property string text
                            required property string reasoning
                            required property string name
                            required property string detail
                            required property string result
                            required property string state
                            required property string choices
                            property bool expanded: false

                            width: ListView.view.width
                            height: body.implicitHeight

                            Column {
                                id: body
                                width: parent.width
                                spacing: 4

                                // user
                                Rectangle {
                                    visible: msg.kind === "user"
                                    anchors.right: parent.right
                                    width: Math.min(body.width * 0.88, userText.contentWidth + 22)
                                    height: userText.contentHeight + 14
                                    radius: 12
                                    color: Theme.p.accentSoft
                                    TextEdit {
                                        id: userText
                                        x: 11; y: 7
                                        width: body.width * 0.88 - 22
                                        readOnly: true; selectByMouse: true
                                        wrapMode: TextEdit.Wrap
                                        textFormat: TextEdit.PlainText
                                        text: msg.kind === "user" ? msg.text : ""
                                        color: Theme.p.text
                                        selectionColor: Theme.p.accent; selectedTextColor: Theme.p.onAccent
                                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                                    }
                                }

                                // assistant: reasoning (folded) + answer
                                Row {
                                    visible: msg.kind === "assistant" && msg.reasoning !== ""
                                    spacing: 4
                                    Icon { name: msg.expanded ? "chevron-down" : "chevron-right"; size: 12; color: Theme.p.text3; anchors.verticalCenter: parent.verticalCenter }
                                    Label {
                                        role: "caption"
                                        text: "Razonamiento"
                                        MouseArea { anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor; onClicked: msg.expanded = !msg.expanded }
                                    }
                                }
                                TextEdit {
                                    visible: msg.kind === "assistant" && msg.expanded
                                    width: body.width
                                    leftPadding: 10
                                    readOnly: true; selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    textFormat: TextEdit.PlainText
                                    text: visible ? msg.reasoning : ""
                                    color: Theme.p.text3
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.smallFontSize; font.italic: true
                                }
                                TextEdit {
                                    visible: (msg.kind === "assistant" || msg.kind === "system") && msg.text !== ""
                                    width: body.width
                                    readOnly: true; selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    textFormat: TextEdit.MarkdownText
                                    text: visible ? msg.text : ""
                                    color: msg.kind === "system" ? Theme.p.text3 : Theme.p.text
                                    selectionColor: Theme.p.accent; selectedTextColor: Theme.p.onAccent
                                    font.family: Theme.fontFamily
                                    font.pixelSize: msg.kind === "system" ? Theme.smallFontSize : Theme.fontSize
                                    onLinkActivated: link => Qt.openUrlExternally(link)
                                }

                                // tool call
                                Rectangle {
                                    visible: msg.kind === "tool"
                                    width: body.width
                                    height: toolCol.implicitHeight + 12
                                    radius: 8
                                    color: Theme.p.surface
                                    border.width: 1
                                    border.color: msg.state === "error" ? Theme.p.red : Theme.p.stroke3
                                    Column {
                                        id: toolCol
                                        x: 9; y: 6
                                        width: parent.width - 18
                                        spacing: 4
                                        RowLayout {
                                            width: parent.width
                                            spacing: 6
                                            Icon { name: "terminal"; size: 13; color: Theme.p.text3 }
                                            Label { text: msg.name; font.weight: Font.DemiBold; font.pixelSize: Theme.smallFontSize }
                                            Label {
                                                Layout.fillWidth: true
                                                text: msg.detail.split("\n")[0]
                                                font.family: Theme.monoFamily; font.pixelSize: Theme.smallFontSize
                                                color: Theme.p.text3
                                                elide: Text.ElideMiddle
                                            }
                                            Label {
                                                role: "caption"
                                                text: msg.state === "running" ? "…" : msg.state === "error" ? "falló" : "hecho"
                                                color: msg.state === "error" ? Theme.p.red : msg.state === "running" ? Theme.p.accent : Theme.p.green
                                            }
                                        }
                                        TextEdit {
                                            visible: msg.expanded
                                            width: parent.width
                                            readOnly: true; selectByMouse: true
                                            wrapMode: TextEdit.WrapAnywhere
                                            textFormat: TextEdit.PlainText
                                            text: visible ? (msg.detail + (msg.result ? "\n\n" + msg.result : "")) : ""
                                            color: Theme.p.text2
                                            font.family: Theme.monoFamily; font.pixelSize: Theme.smallFontSize
                                        }
                                    }
                                    MouseArea {
                                        width: parent.width; height: 28
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: msg.expanded = !msg.expanded
                                    }
                                }

                                // error
                                Row {
                                    visible: msg.kind === "error"
                                    width: body.width
                                    spacing: 6
                                    Icon { name: "circle-alert"; size: 14; color: Theme.p.red }
                                    TextEdit {
                                        width: parent.width - 20
                                        readOnly: true; selectByMouse: true
                                        wrapMode: TextEdit.Wrap
                                        textFormat: TextEdit.PlainText
                                        text: msg.kind === "error" ? msg.text : ""
                                        color: Theme.p.red
                                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                                    }
                                }

                                // approval / clarify
                                Rectangle {
                                    id: promptCard
                                    readonly property bool isPrompt: msg.kind === "approval" || msg.kind === "clarify" || msg.kind === "secret"
                                    readonly property var options: isPrompt ? JSON.parse(msg.choices || "[]") : []
                                    readonly property var choiceNames: ({ once: "Permitir una vez", session: "Esta sesión", always: "Siempre", deny: "Denegar" })
                                    visible: isPrompt
                                    width: body.width
                                    height: promptCol.implicitHeight + 18
                                    radius: 10
                                    color: Theme.p.surface
                                    border.width: 1
                                    border.color: msg.state === "pending" ? Theme.p.orange : Theme.p.stroke3
                                    ColumnLayout {
                                        id: promptCol
                                        x: 10; y: 9
                                        width: parent.width - 20
                                        spacing: 6
                                        RowLayout {
                                            spacing: 6
                                            Icon { name: msg.kind === "clarify" ? "circle-alert" : "lock"; size: 14; color: Theme.p.orange }
                                            Label { text: msg.kind === "approval" ? "Permiso" : msg.kind === "secret" ? "Contraseña" : "Pregunta"; font.weight: Font.DemiBold }
                                            Label {
                                                visible: msg.state !== "pending" && msg.kind !== "clarify" && msg.result !== ""
                                                role: "caption"
                                                text: promptCard.choiceNames[msg.result] || msg.result
                                            }
                                        }
                                        Label { Layout.fillWidth: true; visible: msg.text !== ""; text: msg.text; wrapMode: Text.Wrap; elide: Text.ElideNone }
                                        Label {
                                            Layout.fillWidth: true
                                            visible: msg.detail !== ""
                                            text: msg.detail
                                            wrapMode: Text.WrapAnywhere; elide: Text.ElideNone
                                            font.family: Theme.monoFamily; font.pixelSize: Theme.smallFontSize
                                            color: Theme.p.text2
                                        }
                                        Flow {
                                            Layout.fillWidth: true
                                            visible: msg.state === "pending"
                                            spacing: 6
                                            Repeater {
                                                model: promptCard.options
                                                delegate: IconButton {
                                                    required property string modelData
                                                    text: msg.kind === "approval" ? (promptCard.choiceNames[modelData] || modelData) : modelData
                                                    primary: msg.kind === "approval" && modelData === "once"
                                                    danger: modelData === "deny"
                                                    onClicked: Aquiles.answer(msg.index, modelData)
                                                }
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            visible: (msg.kind === "clarify" || msg.kind === "secret") && msg.state === "pending"
                                            spacing: 6
                                            TextBox {
                                                id: answerBox
                                                Layout.fillWidth: true
                                                password: msg.kind === "secret"
                                                placeholder: msg.kind === "secret" ? "Se envía solo al gateway local" : "Tu respuesta"
                                                onAccepted: sendAnswer.clicked()
                                            }
                                            IconButton {
                                                id: sendAnswer
                                                text: msg.kind === "secret" ? "Enviar" : "Responder"
                                                primary: true
                                                onClicked: { const v = answerBox.text; answerBox.text = ""; Aquiles.answer(msg.index, v) }
                                            }
                                            IconButton { visible: msg.kind === "secret"; text: "Omitir"; onClicked: { answerBox.text = ""; Aquiles.answer(msg.index, "") } }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── sessions ────────────────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.view === "sessions"
                    spacing: 6

                    Label {
                        role: "section"
                        text: Aquiles.isHermes ? "Sesiones de Hermes" : `Chats de ${Aquiles.engineName(Aquiles.backend)}`
                    }
                    ListView {
                        id: sessionList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        model: Aquiles.isHermes ? Aquiles.sessions : Aquiles.chats.filter(c => c.backend === Aquiles.backend)

                        delegate: Rectangle {
                            id: srow
                            required property var modelData
                            readonly property string sessionId: Aquiles.isHermes ? modelData.session_id : modelData.id
                            readonly property bool current: Aquiles.isHermes ? sessionId === Aquiles.sid : sessionId === Aquiles.chatId
                            property bool renaming: false
                            width: ListView.view.width
                            height: 46
                            radius: 8
                            color: current ? Theme.p.accentSoft : hover.containsMouse ? Theme.p.hover : "transparent"

                            MouseArea {
                                id: hover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (Aquiles.isHermes) Aquiles.openSession(srow.sessionId)
                                    else Aquiles.openChat(srow.sessionId)
                                    root.view = "chat"
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                spacing: 6
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    visible: !srow.renaming
                                    Label { Layout.fillWidth: true; text: srow.modelData.title || "Sin título" }
                                    Label {
                                        Layout.fillWidth: true
                                        role: "caption"
                                        text: Aquiles.isHermes
                                            ? [srow.modelData.model, `${srow.modelData.message_count} mensajes`, root.ago(srow.modelData.started_at),
                                                srow.modelData.running ? "en curso" : ""].filter(Boolean).join(" · ")
                                            : [root.ago(srow.modelData.ts), srow.modelData.native ? "reanudable" : ""].filter(Boolean).join(" · ")
                                    }
                                }
                                TextBox {
                                    id: renameBox
                                    Layout.fillWidth: true
                                    visible: srow.renaming
                                    onAccepted: {
                                        if (text.trim()) {
                                            if (Aquiles.isHermes) Aquiles.renameSession(srow.sessionId, text.trim())
                                            else Aquiles.renameChat(srow.sessionId, text.trim())
                                        }
                                        srow.renaming = false
                                    }
                                }
                                IconButton {
                                    icon: "check"; size: 24; tooltip: "Renombrar"
                                    visible: hover.containsMouse || srow.renaming
                                    onClicked: {
                                        if (srow.renaming) { renameBox.accepted(); return }
                                        renameBox.text = srow.modelData.title || ""
                                        srow.renaming = true
                                        renameBox.focusInput()
                                    }
                                }
                                IconButton {
                                    icon: "trash-2"; size: 24; danger: true; tooltip: "Borrar"
                                    visible: hover.containsMouse && !srow.renaming && !(Aquiles.isHermes && srow.modelData.running)
                                    onClicked: {
                                        if (Aquiles.isHermes) Aquiles.deleteSession(srow.sessionId)
                                        else Aquiles.deleteChat(srow.sessionId)
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        visible: sessionList.count === 0
                        Layout.alignment: Qt.AlignHCenter
                        Layout.bottomMargin: 40
                        role: "muted"
                        text: "Aún no hay conversaciones."
                    }
                }

                // ── model manager ───────────────────────────────────────
                ColumnLayout {
                    id: modelPage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.view === "models"
                    spacing: 6
                    readonly property var rows: !visible ? []
                        : (modelFilter.text.trim() === "" ? Aquiles.recentModels().slice(0, 5) : []).concat(Aquiles.modelChoices(modelFilter.text))
                    property int pick: 0
                    onRowsChanged: pick = 0
                    function choose(i) {
                        const m = rows[i]
                        if (!m) return
                        Aquiles.setModel(m.id, m.provider)
                        root.view = "chat"
                        input.forceActiveFocus()
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        TextBox {
                            id: modelFilter
                            Layout.fillWidth: true
                            placeholder: Aquiles.isHermes ? "Buscar modelo: haiku, opus, gemini…" : "Buscar o escribir el nombre de un modelo"
                            onAccepted: modelPage.choose(modelPage.pick)
                            Keys.onPressed: event => {
                                const n = modelPage.rows.length
                                if (!n || (event.key !== Qt.Key_Down && event.key !== Qt.Key_Up)) return
                                modelPage.pick = (modelPage.pick + (event.key === Qt.Key_Down ? 1 : n - 1)) % n
                                modelList.positionViewAtIndex(modelPage.pick, ListView.Contain)
                                event.accepted = true
                            }
                        }
                        IconButton { visible: Aquiles.isHermes; icon: "refresh-cw"; size: 26; tooltip: "Actualizar catálogos"; onClicked: Aquiles.refreshProviders(true) }
                    }
                    Label {
                        Layout.fillWidth: true
                        role: "caption"
                        wrapMode: Text.Wrap; elide: Text.ElideNone
                        text: `Ahora: ${Aquiles.model || "el modelo por defecto"} · ${Aquiles.provider}`
                    }
                    ListView {
                        id: modelList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 1
                        boundsBehavior: Flickable.StopAtBounds
                        model: modelPage.rows
                        delegate: Item {
                            id: mitem
                            required property var modelData
                            required property int index
                            // A heading wherever the group changes (recents, then one per provider).
                            readonly property bool heads: index === 0 || modelPage.rows[index - 1].group !== modelData.group
                            width: ListView.view.width
                            height: (heads ? 24 : 0) + 30
                            Label {
                                visible: mitem.heads
                                x: 2; y: 4
                                role: "section"
                                text: mitem.modelData.group
                            }
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                height: 30
                                radius: 6
                                color: mitem.modelData.current ? Theme.p.accentSoft
                                    : (modelPage.pick === mitem.index || mitemHover.containsMouse) ? Theme.p.hover : "transparent"
                                MouseArea { id: mitemHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: modelPage.choose(mitem.index) }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 8
                                    Label {
                                        Layout.fillWidth: true
                                        text: mitem.modelData.name
                                        font.family: Aquiles.isHermes ? Theme.monoFamily : Theme.fontFamily
                                        font.pixelSize: Aquiles.isHermes ? Theme.smallFontSize : Theme.fontSize
                                        color: mitem.modelData.current ? Theme.p.accent : Theme.p.text
                                        elide: Text.ElideMiddle
                                    }
                                    Label { visible: text !== ""; role: "caption"; text: mitem.modelData.detail }
                                    Icon { visible: mitem.modelData.current; name: "check"; size: 14; color: Theme.p.accent }
                                }
                            }
                        }
                    }
                    Label {
                        visible: modelPage.rows.length === 0
                        Layout.fillWidth: true
                        role: "caption"
                        wrapMode: Text.Wrap; elide: Text.ElideNone
                        text: !Aquiles.isHermes ? "Escribe el nombre exacto del modelo y pulsa Enter."
                            : Aquiles.link !== "open" ? "Conectando con Hermes…"
                            : modelFilter.text.trim() !== "" ? "Ningún proveedor conectado ofrece ese modelo. Añade su API key en Motor."
                            : "No hay proveedores conectados. Añade una API key en Motor."
                    }
                    Label { Layout.fillWidth: true; role: "caption"; text: "↑↓ elegir · Enter aplicar · vale para este chat y los nuevos" }
                }

                // ── engine, models and keys ─────────────────────────────
                Flickable {
                    id: enginePage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.view === "engine"
                    clip: true
                    contentHeight: engineCol.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    property string expanded: ""    // provider slug showing its models
                    property string keyFor: ""      // provider slug showing its key field
                    property string keyError: ""

                    ColumnLayout {
                        id: engineCol
                        width: enginePage.width
                        spacing: 4

                        Label { role: "section"; text: "Motor" }
                        Repeater {
                            model: Aquiles.engines
                            delegate: Rectangle {
                                id: erow
                                required property var modelData
                                readonly property bool selected: Aquiles.backend === modelData.id
                                readonly property bool cli: modelData.id !== "hermes"
                                readonly property bool installed: !cli || !!Aquiles.bins[modelData.id]
                                Layout.fillWidth: true
                                implicitHeight: 44
                                radius: 8
                                color: selected ? Theme.p.accentSoft : ehover.containsMouse ? Theme.p.hover : "transparent"
                                border.width: selected ? 1 : 0
                                border.color: Theme.p.stroke1
                                MouseArea { id: ehover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: { Aquiles.setBackend(erow.modelData.id); if (erow.modelData.id === "hermes") Aquiles.connect() } }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 8
                                    Icon { name: erow.cli ? "terminal" : "sparkles"; size: 16; color: erow.selected ? Theme.p.accent : Theme.p.text2 }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        Label { Layout.fillWidth: true; text: erow.modelData.name }
                                        Label {
                                            Layout.fillWidth: true
                                            role: "caption"
                                            elide: Text.ElideMiddle
                                            color: erow.installed ? Theme.p.text3 : Theme.p.orange
                                            text: !erow.cli ? (Aquiles.link === "open" ? erow.modelData.detail : Aquiles.link === "error" ? Aquiles.linkError.split("\n")[0] : erow.modelData.detail)
                                                : erow.installed ? Aquiles.bins[erow.modelData.id]
                                                : Aquiles.binsReady ? `${erow.modelData.detail}: no instalado` : erow.modelData.detail
                                        }
                                    }
                                    Icon { visible: erow.selected; name: "check"; size: 14; color: Theme.p.accent }
                                }
                            }
                        }

                        Section { text: "Carpeta de trabajo" }
                        TextBox {
                            Layout.fillWidth: true
                            placeholder: Aquiles.home
                            text: Settings.data.aquilesCwd
                            onAccepted: Settings.data.aquilesCwd = text.trim()
                        }
                        Label { role: "caption"; text: "Enter para aplicar; vale para los chats nuevos." }

                        // CLI engines
                        Section { text: "Modelo" }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                font.family: Theme.monoFamily
                                text: Aquiles.model || "por defecto"
                            }
                            IconButton { text: "Cambiar…"; onClicked: root.pickModel("") }
                        }
                        Flow {
                            visible: !Aquiles.isHermes && (Aquiles.cliModels[Aquiles.backend] || []).length > 0
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: Aquiles.isHermes ? [] : (Aquiles.cliModels[Aquiles.backend] || []).slice(0, 8)
                                delegate: IconButton {
                                    required property var modelData
                                    size: 24
                                    text: modelData.name
                                    checked: Aquiles.model === modelData.id
                                    onClicked: Aquiles.setModel(checked ? "" : modelData.id, "")
                                }
                            }
                        }
                        Section { visible: !Aquiles.isHermes; text: "Permisos" }
                        RowLayout {
                            visible: !Aquiles.isHermes
                            spacing: 6
                            Repeater {
                                model: [{ id: "read", name: "Solo lectura" }, { id: "edits", name: "Editar archivos" }, { id: "full", name: "Sin preguntar" }]
                                delegate: IconButton {
                                    required property var modelData
                                    text: modelData.name
                                    checked: Aquiles.permission === modelData.id
                                    danger: modelData.id === "full" && checked
                                    onClicked: Settings.data.aquilesPermission = modelData.id
                                }
                            }
                        }
                        Label {
                            visible: !Aquiles.isHermes
                            Layout.fillWidth: true
                            role: "caption"
                            wrapMode: Text.Wrap; elide: Text.ElideNone
                            text: "La CLI usa su propio inicio de sesión (suscripción o API key): ábrela una vez en una terminal para entrar. «Sin preguntar» deja que ejecute cualquier orden."
                        }
                        IconButton {
                            visible: !Aquiles.isHermes
                            icon: "terminal"
                            text: `Abrir ${Aquiles.backend} en una terminal`
                            enabled: !!Aquiles.bins[Aquiles.backend]
                            onClicked: Session.terminal(Aquiles.bins[Aquiles.backend])
                        }

                        // Hermes: session controls, then every provider
                        Section { visible: Aquiles.isHermes; text: "Sesión" }
                        RowLayout {
                            visible: Aquiles.isHermes
                            spacing: 4
                            Label { role: "muted"; text: "Razonamiento" }
                            Repeater {
                                model: ["none", "low", "medium", "high", "xhigh"]
                                delegate: IconButton {
                                    required property string modelData
                                    size: 24
                                    text: modelData
                                    checked: (Aquiles.info.reasoning_effort || "") === modelData
                                    onClicked: Aquiles.setEffort(modelData)
                                }
                            }
                        }
                        Switch {
                            visible: Aquiles.isHermes
                            text: "YOLO"
                            detail: "ejecuta órdenes sin pedir permiso"
                            checked: !!Aquiles.info.yolo
                            onToggled: Aquiles.setYolo(!Aquiles.info.yolo)
                        }

                        RowLayout {
                            visible: Aquiles.isHermes
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            Label { role: "section"; text: "Proveedores y API keys"; Layout.fillWidth: true }
                            IconButton { icon: "refresh-cw"; size: 22; tooltip: "Actualizar"; onClicked: Aquiles.refreshProviders(true) }
                        }
                        TextBox { id: filter; visible: Aquiles.isHermes; Layout.fillWidth: true; placeholder: "Filtrar proveedores o modelos" }

                        Repeater {
                            model: Aquiles.isHermes ? Aquiles.providers : []
                            delegate: ColumnLayout {
                                id: prow
                                required property var modelData
                                readonly property string q: filter.text.trim().toLowerCase()
                                readonly property var shownModels: (modelData.models || []).filter(m => !q || m.toLowerCase().includes(q)
                                    || modelData.name.toLowerCase().includes(q))
                                readonly property bool open: enginePage.expanded === modelData.slug || (q !== "" && shownModels.length > 0 && shownModels.length <= 12)
                                readonly property bool takesKey: modelData.auth_type === "api_key"
                                visible: !q || modelData.name.toLowerCase().includes(q) || modelData.slug.includes(q) || shownModels.length > 0
                                Layout.fillWidth: true
                                spacing: 2

                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 34
                                    radius: 8
                                    color: phover.containsMouse ? Theme.p.hover : "transparent"
                                    MouseArea { id: phover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (prow.modelData.authenticated) enginePage.expanded = enginePage.expanded === prow.modelData.slug ? "" : prow.modelData.slug
                                            else if (prow.takesKey) { enginePage.keyFor = enginePage.keyFor === prow.modelData.slug ? "" : prow.modelData.slug; enginePage.keyError = "" }
                                        } }
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 4
                                        spacing: 6
                                        Rectangle { implicitWidth: 7; implicitHeight: 7; radius: 4; color: prow.modelData.authenticated ? Theme.p.green : Theme.p.stroke1 }
                                        Label { text: prow.modelData.name; font.weight: prow.modelData.is_current ? Font.DemiBold : Font.Normal }
                                        Label {
                                            Layout.fillWidth: true
                                            role: "caption"
                                            text: prow.modelData.authenticated ? `${prow.modelData.total_models || (prow.modelData.models || []).length} modelos`
                                                : prow.takesKey ? "sin API key" : "se configura con «hermes model»"
                                        }
                                        IconButton {
                                            visible: prow.takesKey
                                            size: 24
                                            text: prow.modelData.authenticated ? "Cambiar key" : "Añadir key"
                                            onClicked: { enginePage.keyFor = enginePage.keyFor === prow.modelData.slug ? "" : prow.modelData.slug; enginePage.keyError = "" }
                                        }
                                        IconButton {
                                            visible: prow.modelData.authenticated && prow.takesKey
                                            size: 24; icon: "trash-2"; danger: true; tooltip: "Quitar la API key"
                                            onClicked: Aquiles.disconnectProvider(prow.modelData.slug, err => enginePage.keyError = err)
                                        }
                                    }
                                }

                                RowLayout {
                                    visible: enginePage.keyFor === prow.modelData.slug
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 21
                                    spacing: 6
                                    TextBox {
                                        id: keyBox
                                        Layout.fillWidth: true
                                        password: true
                                        placeholder: prow.modelData.key_env || "API key"
                                        onAccepted: saveKey.clicked()
                                    }
                                    IconButton {
                                        id: saveKey
                                        text: "Guardar"
                                        primary: true
                                        enabled: keyBox.text.trim() !== ""
                                        onClicked: Aquiles.saveKey(prow.modelData.slug, keyBox.text.trim(), err => {
                                            enginePage.keyError = err
                                            if (!err) { keyBox.text = ""; enginePage.keyFor = ""; enginePage.expanded = prow.modelData.slug }
                                        })
                                    }
                                }
                                Label {
                                    visible: enginePage.keyFor === prow.modelData.slug && enginePage.keyError !== ""
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 21
                                    role: "caption"
                                    color: Theme.p.red
                                    wrapMode: Text.Wrap; elide: Text.ElideNone
                                    text: enginePage.keyError
                                }

                                Repeater {
                                    model: prow.open ? prow.shownModels : []
                                    delegate: Rectangle {
                                        id: mrow
                                        required property string modelData
                                        readonly property bool current: Aquiles.model === modelData && Aquiles.provider === prow.modelData.slug
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 21
                                        implicitHeight: 26
                                        radius: 6
                                        color: current ? Theme.p.accentSoft : mhover.containsMouse ? Theme.p.hover : "transparent"
                                        MouseArea { id: mhover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                            onClicked: { Aquiles.setModel(mrow.modelData, prow.modelData.slug); root.view = "chat" } }
                                        Label {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 8
                                            verticalAlignment: Text.AlignVCenter
                                            text: mrow.modelData
                                            font.family: Theme.monoFamily; font.pixelSize: Theme.smallFontSize
                                            color: mrow.current ? Theme.p.accent : Theme.p.text2
                                            elide: Text.ElideMiddle
                                        }
                                    }
                                }
                            }
                        }
                        Item { implicitHeight: 12 }
                    }
                }

                // ── composer ────────────────────────────────────────────
                Rectangle {
                    id: suggest
                    readonly property var hits: {
                        const t = input.text
                        if (!Aquiles.isHermes || t[0] !== "/" || /\s/.test(t)) return []
                        return Aquiles.commands.filter(c => c[0].startsWith(t)).slice(0, 7)
                    }
                    property int pick: 0
                    onHitsChanged: pick = 0
                    visible: root.view === "chat" && hits.length > 0
                    Layout.fillWidth: true
                    implicitHeight: hits.length * 26 + 8
                    radius: 10
                    color: Theme.p.surface
                    border.width: 1
                    border.color: Theme.p.stroke3
                    Column {
                        x: 4; y: 4
                        width: parent.width - 8
                        Repeater {
                            model: suggest.hits
                            delegate: Rectangle {
                                id: hit
                                required property var modelData
                                required property int index
                                width: parent.width
                                height: 26
                                radius: 6
                                color: index === suggest.pick ? Theme.p.accentSoft : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 8
                                    Label { text: hit.modelData[0]; font.family: Theme.monoFamily; color: Theme.p.accent }
                                    Label { Layout.fillWidth: true; role: "caption"; text: hit.modelData[1] }
                                }
                                MouseArea { anchors.fill: parent; onClicked: { input.text = hit.modelData[0] + " "; input.cursorPosition = input.text.length; input.forceActiveFocus() } }
                            }
                        }
                    }
                }

                Label {
                    visible: root.view === "chat" && Aquiles.busy
                    Layout.fillWidth: true
                    role: "caption"
                    color: Theme.p.accent
                    text: Aquiles.status || "trabajando…"
                }

                Rectangle {
                    visible: root.view === "chat"
                    Layout.fillWidth: true
                    implicitHeight: Math.min(160, Math.max(40, input.contentHeight + 20))
                    radius: 12
                    color: Theme.p.surface
                    border.width: 1
                    border.color: input.activeFocus ? Theme.p.accent : Theme.p.stroke2

                    Flickable {
                        id: inputView
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.rightMargin: 44
                        clip: true
                        contentHeight: input.contentHeight
                        boundsBehavior: Flickable.StopAtBounds

                        TextEdit {
                            id: input
                            width: inputView.width
                            height: Math.max(inputView.height, contentHeight)
                            wrapMode: TextEdit.Wrap
                            textFormat: TextEdit.PlainText
                            selectByMouse: true
                            color: Theme.p.text
                            selectionColor: Theme.p.accentSoft
                            selectedTextColor: Theme.p.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                            focus: true
                            onCursorRectangleChanged: {
                                if (cursorRectangle.y < inputView.contentY) inputView.contentY = cursorRectangle.y
                                else if (cursorRectangle.y + cursorRectangle.height > inputView.contentY + inputView.height)
                                    inputView.contentY = cursorRectangle.y + cursorRectangle.height - inputView.height
                            }

                            function submit() {
                                const t = text.trim()
                                if (!t) return
                                text = ""
                                const m = /^\/models?(?:\s+(.*))?$/.exec(t)
                                if (m) { root.pickModel(m[1] || ""); return }
                                chat.stick = true
                                Aquiles.send(t)
                            }

                            Keys.onPressed: event => {
                                const hits = suggest.hits
                                if (event.key === Qt.Key_Escape) { root.close(); event.accepted = true }
                                else if (hits.length && (event.key === Qt.Key_Down || event.key === Qt.Key_Up)) {
                                    suggest.pick = (suggest.pick + (event.key === Qt.Key_Down ? 1 : hits.length - 1)) % hits.length
                                    event.accepted = true
                                } else if (hits.length && event.key === Qt.Key_Tab) {
                                    text = hits[suggest.pick][0] + " "
                                    cursorPosition = text.length
                                    event.accepted = true
                                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                                    if (hits.length && hits[suggest.pick][0] !== text.trim() && !hits.some(h => h[0] === text.trim())) {
                                        text = hits[suggest.pick][0] + " "
                                        cursorPosition = text.length
                                    } else submit()
                                    event.accepted = true
                                }
                            }

                            Text {
                                visible: !input.text
                                text: Aquiles.busy && Aquiles.isHermes ? "Corrige el rumbo mientras trabaja…" : "Pregunta a AquilesAI"
                                color: Theme.p.text4
                                font: input.font
                            }
                        }
                    }
                    IconButton {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 6
                        size: 28
                        primary: !Aquiles.busy || input.text.trim() !== ""
                        danger: Aquiles.busy && input.text.trim() === ""
                        icon: Aquiles.busy && input.text.trim() === "" ? "x" : "corner-down-left"
                        tooltip: Aquiles.busy && input.text.trim() === "" ? "Detener" : "Enviar (Enter)"
                        onClicked: { if (Aquiles.busy && input.text.trim() === "") Aquiles.stop(); else input.submit() }
                    }
                }

                RowLayout {
                    visible: root.view === "chat"
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        role: "caption"
                        elide: Text.ElideMiddle
                        text: [Aquiles.title, Aquiles.isHermes ? (Aquiles.info.yolo ? "YOLO" : "") : ({ read: "solo lectura", edits: "edita archivos", full: "sin preguntar" })[Aquiles.permission]]
                            .filter(Boolean).join(" · ")
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.view = "engine" }
                    }
                    Label { role: "caption"; text: Aquiles.usageText }
                    Rectangle {
                        radius: 9
                        implicitHeight: 18
                        implicitWidth: Math.min(modelTag.implicitWidth, 190) + 14
                        color: modelTagHover.containsMouse ? Theme.p.hover : Theme.p.surface
                        border.width: 1
                        border.color: Theme.p.stroke2
                        Label { id: modelTag; anchors.fill: parent; anchors.leftMargin: 7; anchors.rightMargin: 7; role: "caption"
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideLeft
                            text: (Aquiles.model || "modelo por defecto") + " ▾" }
                        MouseArea { id: modelTagHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.pickModel("") }
                    }
                }
            }
        }
    }

    IpcHandler {
        target: "aquiles"
        function toggle(): void { root.toggle() }
        function open(): void { root.show() }
        function close(): void { root.close() }
        function ask(text: string): void { root.ask(text) }
        function newChat(): void { Aquiles.newChat(); root.show() }
        function stop(): void { Aquiles.stop() }
        function engine(id: string): void { if (Aquiles.engines.some(e => e.id === id)) Aquiles.setBackend(id) }
    }
}
