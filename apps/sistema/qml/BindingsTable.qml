import QtQuick
import QtQuick.Layouts
import "Bindings.js" as Bindings

/// A table of config.toml edited line by line: mouse, gesture and touch bindings, and [env].
ColumnLayout {
    id: root

    /// {table, keyLabel, keyHint, valueLabel, valueHint, plain}; `plain` tables have no built-in entries.
    required property var spec

    readonly property string modKey: {
        Config.revision
        return String(Config.has(["mod_key"]) ? Config.get(["mod_key"]) : App.fallback(["mod_key"]))
    }
    readonly property var rows: {
        Config.revision
        if (spec.plain)
            return Bindings.rows([], Config.entries(spec.table), k => k, true)
        const off = Config.has(["bindings", "disable_defaults"]) ? Config.get(["bindings", "disable_defaults"]) : []
        return Bindings.rows(App.fallbackEntries(spec.table), Config.entries(spec.table), norm, off.indexOf(spec.table[0]) >= 0)
    }

    function norm(key) {
        return Bindings.normalize(key, modKey, null)
    }
    function find(key) {
        const id = spec.plain ? key : norm(key)
        return rows.find(r => r.id === id) || null
    }

    spacing: 0

    Repeater {
        model: root.rows

        RowLayout {
            id: line

            required property var modelData

            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 10
            Layout.topMargin: 5
            Layout.bottomMargin: 5
            spacing: 8

            Text {
                Layout.preferredWidth: 230
                text: line.modelData.key
                color: line.modelData.state === "off" ? Theme.faint : Theme.text
                font.pixelSize: Theme.body
                font.family: "monospace"
                font.strikeout: line.modelData.state === "off"
                elide: Text.ElideRight
            }
            TField {
                Layout.fillWidth: true
                enabled: line.modelData.state !== "off"
                text: line.modelData.state === "off" ? line.modelData.defaultValue : line.modelData.value
                onEditingFinished: {
                    const next = text.trim()
                    if (next === "" || next === line.modelData.value)
                        text = line.modelData.value
                    else if (!Config.apply(Bindings.assign(root.spec.table, line.modelData, line.modelData.key, next)))
                        text = line.modelData.value
                }
            }
            Text {
                Layout.preferredWidth: 78
                text: ({ default: qsTr("de fábrica"), changed: qsTr("cambiado"), off: qsTr("desactivado"), custom: qsTr("tuyo") })[line.modelData.state]
                color: line.modelData.state === "default" ? Theme.faint : Theme.accent
                font.pixelSize: Theme.small
            }
            TButton {
                visible: line.modelData.state === "changed" || line.modelData.state === "off"
                text: qsTr("Restaurar")
                onClicked: Config.apply(Bindings.restore(root.spec.table, line.modelData))
            }
            TButton {
                visible: line.modelData.state !== "off"
                text: line.modelData.state === "custom" ? qsTr("Eliminar") : qsTr("Desactivar")
                destructive: true
                onClicked: Config.apply(Bindings.remove(root.spec.table, line.modelData))
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 14
        Layout.rightMargin: 10
        Layout.topMargin: 8
        Layout.bottomMargin: 10
        spacing: 8

        function add() {
            const key = newKey.text.trim()
            const value = newValue.text.trim()
            if (key === "" || value === "")
                return
            if (Config.apply(Bindings.assign(root.spec.table, root.find(key), key, value))) {
                newKey.clear()
                newValue.clear()
            }
        }

        TField {
            id: newKey

            Layout.preferredWidth: 230
            placeholderText: root.spec.keyLabel + " (" + root.spec.keyHint + ")"
            onAccepted: parent.add()
        }
        TField {
            id: newValue

            Layout.fillWidth: true
            placeholderText: root.spec.valueLabel + " (" + root.spec.valueHint + ")"
            onAccepted: parent.add()
        }
        TButton {
            text: qsTr("Añadir")
            enabled: newKey.text.trim() !== "" && newValue.text.trim() !== ""
            onClicked: parent.add()
        }
    }
}
