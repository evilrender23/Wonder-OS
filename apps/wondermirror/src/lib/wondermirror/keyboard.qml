import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io

// The window of `wondermirror keyboard --pick`: the keyboard layouts Windows
// apps can type with. Choosing one runs `wondermirror keyboard <key>`, which
// saves it and switches the running apps. Everything it shows comes from the
// command line, in WONDERMIRROR_KEYBOARD; the colours are the shell's palette.
FloatingWindow {
    id: win

    readonly property var scene: JSON.parse(Quickshell.env("WONDERMIRROR_KEYBOARD") || "{}")
    readonly property bool es: scene.spanish === true
    readonly property var p: scene.dark ? ({
        chrome: "#0d1117", text: "#e6edf3", text3: "#7d8590", stroke: "#30363d",
        hover: "#17243a", active: "#1d2e4f", accent: "#4a84fe", red: "#f85149", field: "#161b22"
    }) : ({
        chrome: "#f8f9fe", text: "#242528", text3: "#7e7f83", stroke: "#c3d1f0",
        hover: "#e7ecf7", active: "#d9e2f3", accent: "#0053fd", red: "#cf2d56", field: "#ffffff"
    })
    readonly property string fontFamily: "Noto Sans"

    property string current: scene.current || ""
    property string pending: ""          // the layout being applied
    property string message: ""
    property bool failed: false

    // "" is «the keyboard of this computer», first in the list.
    readonly property var rows: {
        const all = [{ key: "", name: (es ? "Automático: el de este equipo" : "Automatic: this computer's"), detail: scene.host || "" }]
        for (const layout of scene.layouts || []) all.push({ key: layout.key, name: layout.name, detail: "" })
        const wanted = filter.text.trim().toLowerCase()
        return wanted === "" ? all : all.filter(row => (row.name + " " + row.key + " " + row.detail).toLowerCase().includes(wanted))
    }

    function choose(key) {
        if (apply.running) return
        pending = key
        failed = false
        message = es ? "Cambiando…" : "Switching…"
        apply.command = (scene.command || ["wondermirror", "keyboard"]).concat(["--json", key === "" ? "auto" : key])
        apply.running = true
    }

    title: es ? "Teclado de Wonder Mirror" : "Wonder Mirror Keyboard"
    color: p.chrome
    implicitWidth: 360
    implicitHeight: 520
    minimumSize: Qt.size(320, 320)
    visible: true
    onVisibleChanged: if (!visible) Qt.quit()

    Process {
        id: apply

        stdout: StdioCollector { id: said }
        stderr: StdioCollector { id: complained }
        onExited: (code) => {
            win.failed = code !== 0
            if (win.failed) {
                win.message = complained.text.trim().replace(/^wondermirror: /, "")
                return
            }
            win.current = win.pending
            let answer = {}
            try { answer = JSON.parse(said.text) } catch (error) {}
            const name = answer.layout ? answer.layout.name : ""
            // Windows switches at once only while it has apps open.
            win.message = answer.live
                ? (win.es ? "Las apps de Windows ya escriben con el teclado " + name + "." : "Windows apps now type with the " + name + " keyboard.")
                : (win.es ? "Las apps de Windows escribirán con el teclado " + name + " la próxima vez que se abran." : "Windows apps will type with the " + name + " keyboard the next time they open.")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Text {
            text: win.es ? "Teclado de las apps de Windows" : "Keyboard of the Windows apps"
            color: win.p.text
            font.family: win.fontFamily
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 32
            radius: 8
            color: win.p.field
            border.color: filter.activeFocus ? win.p.accent : win.p.stroke

            TextInput {
                id: filter

                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                verticalAlignment: TextInput.AlignVCenter
                color: win.p.text
                selectionColor: win.p.accent
                font.family: win.fontFamily
                font.pixelSize: 13
                clip: true
                focus: true
                Keys.onEscapePressed: Qt.quit()
                Keys.onDownPressed: list.incrementCurrentIndex()
                Keys.onUpPressed: list.decrementCurrentIndex()
                Keys.onReturnPressed: if (list.currentIndex >= 0 && list.currentIndex < win.rows.length) win.choose(win.rows[list.currentIndex].key)
                onTextChanged: list.currentIndex = 0

                Text {
                    anchors.fill: parent
                    verticalAlignment: Text.AlignVCenter
                    visible: filter.text === ""
                    text: win.es ? "Buscar" : "Search"
                    color: win.p.text3
                    font: filter.font
                }
            }
        }

        ListView {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: win.rows
            highlightMoveDuration: 0

            delegate: Rectangle {
                id: row

                required property var modelData
                required property int index
                readonly property bool chosen: modelData.key === win.current

                width: list.width
                height: 34
                radius: 8
                color: chosen ? win.p.active : mouse.containsMouse || list.currentIndex === index ? win.p.hover : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.name
                        color: win.p.text
                        elide: Text.ElideRight
                        font.family: win.fontFamily
                        font.pixelSize: 13
                        font.weight: row.chosen ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        text: row.modelData.detail !== "" ? row.modelData.detail : row.modelData.key
                        color: win.p.text3
                        font.family: win.fontFamily
                        font.pixelSize: 11
                    }
                    Text {
                        text: "✓"
                        opacity: row.chosen ? 1 : 0
                        color: win.p.accent
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                }

                MouseArea {
                    id: mouse

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: win.choose(row.modelData.key)
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: win.message !== ""
            text: win.message
            color: win.failed ? win.p.red : win.p.text3
            wrapMode: Text.WordWrap
            font.family: win.fontFamily
            font.pixelSize: 11
        }
    }
}
