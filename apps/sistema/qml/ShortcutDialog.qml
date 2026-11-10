import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Actions.js" as Actions
import "Bindings.js" as Bindings

/// Creates a keyboard shortcut, or edits the one in `row`.
Popup {
    id: root

    required property string modKey
    required property var rows
    required property var norm

    property var row: null
    property var mods: []
    property string actionId: "spawn"
    property bool recording: false

    readonly property var action: Actions.find(actionId)
    readonly property string key: keyField.text.trim()
    readonly property string combo: Bindings.compose(mods, key, modKey)
    readonly property bool keyKnown: key === "" || App.canonicalKey(key) !== ""
    readonly property bool comboValid: keyKnown && (key !== "" || mods.length > 0)
    readonly property string value: {
        if (!action)
            return customField.text.trim()
        if (!action.arg)
            return action.id
        const arg = Actions.args[action.arg] ? argChoice.currentValue : argField.text.trim()
        return arg ? Actions.compose(action.id, arg) : ""
    }
    /// The shortcut already on this combination, which saving replaces.
    readonly property var taken: {
        if (!comboValid)
            return null
        const id = norm(combo)
        const other = rows.find(r => r.id === id)
        return other && !(row && row.id === id) ? other : null
    }
    /// A switched-off default is replaced without a word: it does nothing now.
    readonly property bool replaces: taken !== null && taken.state !== "off"

    function openFor(target) {
        row = target
        recording = false
        const parsedCombo = Bindings.parseCombo(target ? target.key : "mod", modKey)
        mods = parsedCombo.mods
        keyField.text = parsedCombo.key
        const parsed = Actions.parse(target ? target.value : "spawn")
        const known = Actions.find(parsed.id)
        actionId = known ? parsed.id : ""
        actionChoice.currentIndex = known ? Actions.list.indexOf(known) : -1
        customField.text = known ? "" : (target ? target.value : "")
        argField.text = parsed.arg
        const options = known && Actions.args[known.arg]
        if (options)
            argChoice.currentIndex = Math.max(0, options.findIndex(o => o[0] === parsed.arg))
        open()
        if (!target)
            keyField.forceActiveFocus()
    }
    function toggle(mod) {
        mods = mods.indexOf(mod) >= 0 ? mods.filter(m => m !== mod) : mods.concat([mod])
    }
    function save() {
        if (!comboValid || value === "")
            return
        if (Config.apply(Bindings.save(["keybindings"], row, combo, value, norm, taken)))
            close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    modal: true
    focus: true
    padding: 22
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        radius: 14
        color: Theme.card
        border.width: 1
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: "#66000000"
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            text: root.row ? "Editar atajo" : "Nuevo atajo"
            color: Theme.text
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }

        Text {
            text: "Combinación"
            color: Theme.dim
            font.pixelSize: Theme.small
        }
        RowLayout {
            spacing: 6

            Repeater {
                model: ["super", "ctrl", "alt", "shift"].concat(root.modKey === "mod3" || root.mods.indexOf("mod3") >= 0 ? ["mod3"] : [])

                TButton {
                    required property string modelData

                    text: Bindings.modifierGlyphs[modelData] + (modelData === root.modKey ? " Mod" : (modelData === "super" ? " Super" : (modelData === "shift" ? " Mayús" : "")))
                    primary: root.mods.indexOf(modelData) >= 0
                    onClicked: root.toggle(modelData)
                }
            }
            Text {
                text: "+"
                color: Theme.dim
                font.pixelSize: Theme.body
            }
            TField {
                id: keyField

                Layout.fillWidth: true
                placeholderText: "tecla: t, Return, F5, Print…"
            }
        }
        RowLayout {
            spacing: 10

            Rectangle {
                id: recorder

                implicitWidth: 170
                implicitHeight: 30
                radius: 7
                color: root.recording ? Theme.accent : Theme.field
                activeFocusOnTab: true

                Text {
                    anchors.centerIn: parent
                    text: root.recording ? "Pulsa las teclas…" : "⏺ Grabar pulsación"
                    color: root.recording ? "#ffffff" : Theme.text
                    font.pixelSize: Theme.body
                }
                TapHandler {
                    onTapped: {
                        root.recording = !root.recording
                        if (root.recording)
                            recorder.forceActiveFocus()
                    }
                }
                onActiveFocusChanged: {
                    if (!activeFocus)
                        root.recording = false
                }
                Keys.onPressed: event => {
                    if (!root.recording)
                        return
                    event.accepted = true
                    if ([Qt.Key_Shift, Qt.Key_Control, Qt.Key_Alt, Qt.Key_Meta, Qt.Key_AltGr, Qt.Key_Super_L, Qt.Key_Super_R].indexOf(event.key) >= 0)
                        return
                    const layout = Config.has(["input", "keyboard", "layout"]) ? Config.get(["input", "keyboard", "layout"]) : App.fallback(["input", "keyboard", "layout"])
                    const variant = Config.has(["input", "keyboard", "variant"]) ? Config.get(["input", "keyboard", "variant"]) : App.fallback(["input", "keyboard", "variant"])
                    const name = App.keyName(event.nativeScanCode, event.text, String(layout ?? ""), String(variant ?? ""))
                    if (name === "")
                        return
                    const pressed = []
                    if (event.modifiers & Qt.MetaModifier)
                        pressed.push("super")
                    if (event.modifiers & Qt.ControlModifier)
                        pressed.push("ctrl")
                    if (event.modifiers & Qt.AltModifier)
                        pressed.push("alt")
                    if (event.modifiers & Qt.ShiftModifier)
                        pressed.push("shift")
                    root.mods = pressed
                    keyField.text = name
                    root.recording = false
                }
            }
            Text {
                Layout.fillWidth: true
                text: "Las combinaciones que ya tienen un atajo no llegan a esta ventana: márcalas a mano."
                color: Theme.dim
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
        }
        RowLayout {
            spacing: 8

            KeyCaps {
                combo: root.combo
                modKey: root.modKey
            }
            Text {
                Layout.fillWidth: true
                text: !root.keyKnown ? "«" + root.key + "» no es el nombre de ninguna tecla."
                    : (root.replaces ? "Ahora hace «" + Actions.describe(root.taken.value) + "»; se reemplazará."
                    : (root.key === "" && root.mods.length > 0 ? "Solo modificadores: se dispara al soltarlos." : ""))
                color: !root.keyKnown || root.replaces ? Theme.danger : Theme.dim
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
            }
        }

        Text {
            Layout.topMargin: 6
            text: "Acción"
            color: Theme.dim
            font.pixelSize: Theme.small
        }
        ComboBox {
            id: actionChoice

            Layout.fillWidth: true
            implicitHeight: 30
            font.pixelSize: Theme.body
            model: Actions.list.map(a => a.group + " · " + a.label)
            displayText: currentIndex < 0 ? "Otra (escrita a mano)" : currentText
            onActivated: index => root.actionId = Actions.list[index].id
        }
        TField {
            id: customField

            Layout.fillWidth: true
            visible: !root.action
            placeholderText: "acción [argumento]"
        }
        TField {
            id: argField

            Layout.fillWidth: true
            visible: root.action !== null && root.action.arg !== undefined && !Actions.args[root.action.arg]
            placeholderText: !root.action ? "" : ({
                command: "orden, p. ej. firefox o sh -c '…'",
                bookmark: "nombre del marcador, p. ej. 1",
                layout: "next, prev o un índice desde 0"
            })[root.action.arg] || ""
            onAccepted: root.save()
        }
        ComboBox {
            id: argChoice

            Layout.fillWidth: true
            implicitHeight: 30
            font.pixelSize: Theme.body
            visible: root.action !== null && Actions.args[root.action.arg] !== undefined
            textRole: "text"
            valueRole: "value"
            model: visible ? Actions.args[root.action.arg].map(o => ({ value: o[0], text: o[1] })) : []
        }
        Text {
            Layout.fillWidth: true
            visible: text !== ""
            text: root.action && root.action.hint ? root.action.hint : ""
            color: Theme.dim
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.topMargin: 8
            spacing: 8

            Item {
                Layout.fillWidth: true
            }
            TButton {
                text: "Cancelar"
                onClicked: root.close()
            }
            TButton {
                text: "Guardar"
                primary: true
                enabled: root.comboValid && root.value !== ""
                onClicked: root.save()
            }
        }
    }
}
