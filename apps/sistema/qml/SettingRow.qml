import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

/// One setting: its name, what it does, and the control for its type.
Item {
    id: root

    required property var setting

    /// Whether config.toml sets it; otherwise the control shows the compositor's default.
    readonly property bool modified: {
        Config.revision
        return Config.has(setting.path)
    }
    readonly property var value: {
        Config.revision
        return Config.has(setting.path) ? Config.get(setting.path) : App.fallback(setting.path)
    }
    readonly property string kind: ({ bool: "bool", int: "int", float: "float", lines: "", flags: "" })[setting.type] ?? "string"

    function commit(next) {
        if (JSON.stringify(next) === JSON.stringify(value))
            return true
        return Config.set(setting.path, next, kind)
    }

    /// "#rrggbbaa", as the config writes a colour, in Qt's "#aarrggbb".
    function qtColor(text) {
        const t = String(text || "")
        return t.length === 9 ? "#" + t.slice(7, 9) + t.slice(1, 7) : t
    }

    implicitHeight: layout.implicitHeight + 20

    RowLayout {
        id: layout

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                spacing: 6

                Text {
                    text: root.setting.label + (root.setting.unit ? " (" + root.setting.unit + ")" : "")
                    color: Theme.text
                    font.pixelSize: Theme.body
                    wrapMode: Text.WordWrap
                    Layout.maximumWidth: 320
                }
                Rectangle {
                    visible: root.setting.startup === true
                    radius: 4
                    color: Theme.field
                    implicitWidth: startupText.implicitWidth + 10
                    implicitHeight: 18

                    Text {
                        id: startupText

                        anchors.centerIn: parent
                        text: qsTr("al reiniciar la sesión")
                        color: Theme.dim
                        font.pixelSize: 11
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
            }
            Text {
                visible: text !== ""
                text: root.setting.desc
                color: Theme.dim
                font.pixelSize: Theme.small
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Loader {
            Layout.alignment: Qt.AlignVCenter
            sourceComponent: ({
                bool: boolControl, int: numberControl, float: numberControl, choice: choiceControl,
                color: colorControl, file: fileControl, lines: linesControl, flags: flagsControl
            })[root.setting.type] || textControl
        }

        TButton {
            text: "↺"
            tip: qsTr("Volver al valor de fábrica")
            opacity: root.modified ? 1 : 0
            enabled: root.modified
            implicitWidth: 30
            leftPadding: 0
            rightPadding: 0
            onClicked: Config.unset(root.setting.path)
        }
    }

    Component {
        id: boolControl

        Switch {
            id: toggle

            property bool shown: root.value === true

            padding: 0
            indicator: Rectangle {
                implicitWidth: 42
                implicitHeight: 24
                radius: 12
                color: toggle.checked ? Theme.accent : Theme.faint

                Rectangle {
                    x: toggle.checked ? parent.width - width - 2 : 2
                    y: 2
                    width: 20
                    height: 20
                    radius: 10
                    color: "#ffffff"

                    Behavior on x {
                        NumberAnimation {
                            duration: 100
                        }
                    }
                }
            }

            onShownChanged: checked = shown
            Component.onCompleted: checked = shown
            onToggled: {
                if (!root.commit(checked))
                    checked = shown
            }
        }
    }

    Component {
        id: numberControl

        RowLayout {
            id: number

            property var shown: root.value
            readonly property bool whole: root.setting.type === "int"
            readonly property real step: root.setting.step || 1

            function format(v) {
                if (v === undefined || v === null)
                    return ""
                return String(whole ? Math.round(v) : Number(Number(v).toFixed(4)))
            }
            function push(next) {
                let n = whole ? Math.round(next) : Number(Number(next).toFixed(4))
                if (isNaN(n)) {
                    field.text = format(shown)
                    return
                }
                if (root.setting.min !== undefined)
                    n = Math.max(root.setting.min, n)
                if (root.setting.max !== undefined)
                    n = Math.min(root.setting.max, n)
                root.commit(n)
                field.text = format(root.value)
            }

            spacing: 4
            onShownChanged: field.text = format(shown)
            Component.onCompleted: field.text = format(shown)

            TButton {
                text: "−"
                implicitWidth: 28
                leftPadding: 0
                rightPadding: 0
                autoRepeat: true
                onClicked: number.push(Number(root.value) - number.step)
            }
            TField {
                id: field

                implicitWidth: 72
                horizontalAlignment: Text.AlignHCenter
                onEditingFinished: number.push(Number(text.replace(",", ".")))
            }
            TButton {
                text: "+"
                implicitWidth: 28
                leftPadding: 0
                rightPadding: 0
                autoRepeat: true
                onClicked: number.push(Number(root.value) + number.step)
            }
        }
    }

    Component {
        id: choiceControl

        ComboBox {
            property var shown: root.value

            function sync() {
                const options = root.setting.options
                for (let i = 0; i < options.length; i++) {
                    if (options[i][0] === String(shown)) {
                        currentIndex = i
                        return
                    }
                }
                currentIndex = -1
            }

            implicitWidth: 250
            implicitHeight: 30
            font.pixelSize: Theme.body
            model: root.setting.options.map(o => o[1])
            displayText: currentIndex < 0 ? String(shown ?? "") : currentText
            onShownChanged: sync()
            Component.onCompleted: sync()
            onActivated: index => {
                if (!root.commit(root.setting.options[index][0]))
                    sync()
            }
        }
    }

    Component {
        id: textControl

        TField {
            property var shown: root.value

            implicitWidth: 250
            placeholderText: root.setting.hint || ""
            onShownChanged: text = shown ?? ""
            Component.onCompleted: text = shown ?? ""
            onEditingFinished: {
                // An emptied field goes back to the default rather than writing "".
                if (text.trim() === "" ? (root.modified && !Config.unset(root.setting.path)) : !root.commit(text.trim()))
                    text = shown ?? ""
            }
        }
    }

    Component {
        id: colorControl

        RowLayout {
            property var shown: root.value

            spacing: 6
            onShownChanged: colorField.text = shown ?? ""
            Component.onCompleted: colorField.text = shown ?? ""

            Rectangle {
                implicitWidth: 30
                implicitHeight: 30
                radius: 7
                color: root.qtColor(root.value) || "transparent"
                border.width: 1
                border.color: Theme.border

                TapHandler {
                    onTapped: {
                        colorDialog.selectedColor = root.qtColor(root.value) || "#000000"
                        colorDialog.open()
                    }
                }
            }
            TField {
                id: colorField

                implicitWidth: 110
                placeholderText: "#rrggbb"
                onEditingFinished: {
                    if (text.trim() === "" ? (root.modified && !Config.unset(root.setting.path)) : !root.commit(text.trim()))
                        text = root.value ?? ""
                }
            }
            ColorDialog {
                id: colorDialog

                options: ColorDialog.ShowAlphaChannel
                onAccepted: {
                    const c = selectedColor
                    const hex = n => Math.round(n * 255).toString(16).padStart(2, "0")
                    root.commit("#" + hex(c.r) + hex(c.g) + hex(c.b) + (c.a < 1 ? hex(c.a) : ""))
                }
            }
        }
    }

    Component {
        id: fileControl

        RowLayout {
            property var shown: root.value

            spacing: 6
            onShownChanged: fileField.text = shown ?? ""
            Component.onCompleted: fileField.text = shown ?? ""

            TField {
                id: fileField

                implicitWidth: 250
                placeholderText: qsTr("~/Imágenes/fondo.jpg")
                onEditingFinished: {
                    if (text.trim() === "" ? (root.modified && !Config.unset(root.setting.path)) : !root.commit(text.trim()))
                        text = root.value ?? ""
                }
            }
            TButton {
                text: qsTr("Elegir…")
                onClicked: fileDialog.open()
            }
            FileDialog {
                id: fileDialog

                onAccepted: root.commit(App.localPath(selectedFile))
            }
        }
    }

    Component {
        id: linesControl

        ColumnLayout {
            id: lines

            property var shown: root.value
            readonly property string saved: (shown || []).join("\n")

            function parsed() {
                return area.text.split("\n").map(l => l.trim()).filter(l => l !== "")
            }

            spacing: 6
            onSavedChanged: area.text = saved
            Component.onCompleted: area.text = saved

            ScrollView {
                implicitWidth: 360
                implicitHeight: 96

                TextArea {
                    id: area

                    font.pixelSize: Theme.body
                    color: Theme.text
                    placeholderText: root.setting.hint || ""
                    placeholderTextColor: Theme.faint
                    wrapMode: TextEdit.NoWrap
                    background: Rectangle {
                        radius: 7
                        color: Theme.field
                        border.width: area.activeFocus ? 2 : 0
                        border.color: Theme.accent
                    }
                }
            }
            TButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Guardar")
                primary: true
                visible: lines.parsed().join("\n") !== lines.saved
                onClicked: root.commit(lines.parsed())
            }
        }
    }

    Component {
        id: flagsControl

        RowLayout {
            spacing: 10

            Repeater {
                model: root.setting.options

                CheckBox {
                    id: flag

                    required property var modelData
                    readonly property bool shown: (root.value || []).indexOf(modelData[0]) >= 0

                    text: modelData[1]
                    font.pixelSize: Theme.body
                    onShownChanged: checked = shown
                    Component.onCompleted: checked = shown
                    onToggled: {
                        const next = root.setting.options.map(o => o[0]).filter(name =>
                            name === flag.modelData[0] ? flag.checked : (root.value || []).indexOf(name) >= 0)
                        if (!root.commit(next))
                            checked = shown
                    }
                }
            }
        }
    }
}
