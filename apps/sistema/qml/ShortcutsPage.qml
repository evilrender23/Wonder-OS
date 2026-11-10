import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Actions.js" as Actions
import "Bindings.js" as Bindings
import "Search.js" as Search

/// Every keyboard shortcut of the desktop: the built-in ones and the user's own.
Item {
    id: root

    /// What to search for on arrival, when the sidebar's search led here.
    property string startQuery: ""

    readonly property var table: ["keybindings"]
    readonly property string modKey: {
        Config.revision
        return String(Config.has(["mod_key"]) ? Config.get(["mod_key"]) : App.fallback(["mod_key"]))
    }
    readonly property var rows: {
        Config.revision
        const off = Config.has(["bindings", "disable_defaults"]) ? Config.get(["bindings", "disable_defaults"]) : []
        return Bindings.rows(App.fallbackEntries(table), Config.entries(table), norm, off.indexOf("keys") >= 0)
    }
    readonly property var shown: {
        const needle = Search.words(search.text)
        const order = ["Tuyos", "Abrir", "Ventana", "Navegar", "Sistema", "Otros"]
        const found = rows.map(r => Object.assign({}, r, {
            what: Actions.describe(r.value),
            section: r.state === "custom" ? "Tuyos" : Actions.group(r.value)
        })).filter(r => Search.has(Search.fold([r.what, r.value, r.key, Bindings.caps(r.key, modKey).join(" ")].join(" ")), needle))
          .sort((a, b) => order.indexOf(a.section) - order.indexOf(b.section)
              || a.what.localeCompare(b.what) || a.key.localeCompare(b.key))
        // A heading is a row of the model: ListView sections need a role, which a JS array lacks.
        const out = []
        for (const r of found) {
            if (out.length === 0 || out[out.length - 1].section !== r.section)
                out.push({ heading: true, section: r.section, what: "", key: "", value: "", state: "" })
            out.push(r)
        }
        return out
    }

    function norm(key) {
        return Bindings.normalize(key, modKey, name => App.canonicalKey(name))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.leftMargin: 28
            Layout.rightMargin: 24
            Layout.topMargin: 22
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: qsTr("Atajos de teclado")
                color: Theme.text
                font.pixelSize: 22
                font.weight: Font.DemiBold
            }
            TField {
                id: search

                implicitWidth: 220
                placeholderText: qsTr("Buscar")
                text: root.startQuery
            }
            TButton {
                text: qsTr("＋ Nuevo atajo")
                primary: true
                onClicked: dialog.openFor(null)
            }
        }
        Text {
            Layout.leftMargin: 28
            Layout.rightMargin: 24
            Layout.topMargin: 6
            Layout.fillWidth: true
            text: qsTr("«Mod» es la tecla %1 (se cambia en General). Los cambios se aplican al momento.").arg(Bindings.modifierGlyphs[root.modKey])
            color: Theme.dim
            font.pixelSize: Theme.small
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            Layout.topMargin: 14
            radius: Theme.radius
            color: Theme.card
            border.width: 1
            border.color: Theme.border

            ListView {
                id: list

                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.shown
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: line

                    required property var modelData

                    readonly property bool heading: modelData.heading === true

                    width: list.width
                    height: heading ? 30 : 44
                    color: heading ? Theme.sidebar : hover.hovered ? Theme.hover : "transparent"

                    HoverHandler {
                        id: hover
                        enabled: !line.heading
                    }
                    TapHandler {
                        enabled: !line.heading
                        onDoubleTapped: dialog.openFor(line.modelData)
                    }
                    Text {
                        visible: line.heading
                        anchors.verticalCenter: parent.verticalCenter
                        x: 14
                        text: Actions.groupLabel(line.modelData.section)
                        color: Theme.dim
                        font.pixelSize: Theme.small
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        visible: !line.heading
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 10
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: line.modelData.state === "off" ? Actions.describe(line.modelData.defaultValue) : line.modelData.what
                            color: line.modelData.state === "off" ? Theme.faint : Theme.text
                            font.pixelSize: Theme.body
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: line.modelData.state === "changed" || line.modelData.state === "off"
                            text: line.modelData.state === "off" ? qsTr("desactivado") : qsTr("cambiado")
                            color: Theme.accent
                            font.pixelSize: Theme.small
                        }
                        KeyCaps {
                            combo: line.modelData.key
                            modKey: root.modKey
                            off: line.modelData.state === "off"
                        }
                        TButton {
                            visible: hover.hovered && line.modelData.state !== "off"
                            text: qsTr("Editar")
                            onClicked: dialog.openFor(line.modelData)
                        }
                        TButton {
                            visible: hover.hovered && (line.modelData.state === "changed" || line.modelData.state === "off")
                            text: qsTr("Restaurar")
                            onClicked: Config.apply(Bindings.restore(root.table, line.modelData))
                        }
                        TButton {
                            visible: hover.hovered && line.modelData.state !== "off"
                            text: line.modelData.state === "custom" ? qsTr("Eliminar") : qsTr("Desactivar")
                            destructive: true
                            onClicked: Config.apply(Bindings.remove(root.table, line.modelData))
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("Ningún atajo coincide.")
                    color: Theme.dim
                    font.pixelSize: Theme.body
                }
            }
        }
    }

    ShortcutDialog {
        id: dialog

        modKey: root.modKey
        rows: root.rows
        norm: root.norm
    }
}
