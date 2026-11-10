import QtQuick
import QtQuick.Controls.Basic

/// Every command of the app, and every object of the database, one search away.
Popup {
    id: palette

    /// [{id, title, section, shortcut, symbol, run, enabled}]
    property var commands: []
    property string query
    signal chosen(var command)

    /// What matches the query, best first; each section keeps its entries together.
    readonly property var matches: {
        const needle = fold(query.trim())
        const found = []
        for (const command of commands) {
            if (command.hidden && needle === "")
                continue
            const score = needle === "" ? 1 : Math.max(rank(needle, fold(command.title)), rank(needle, fold(command.section + " " + command.title)) - 1)
            if (score > 0)
                found.push({command: command, score: score})
        }
        if (needle === "")
            return found.map(entry => entry.command)
        // The best entry of a section decides where the section goes.
        const best = {}
        for (const entry of found)
            best[entry.command.section] = Math.max(best[entry.command.section] ?? 0, entry.score)
        found.sort((a, b) => (best[b.command.section] - best[a.command.section])
            || a.command.section.localeCompare(b.command.section) || (b.score - a.score))
        return found.map(entry => entry.command)
    }

    /// Lower case and no accents: «diseno» finds «Diseño».
    function fold(text) {
        return String(text).normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase()
    }
    /// How well `needle` is found in `text`, letters in order: 0 is not at all.
    function rank(needle, text) {
        const at = text.indexOf(needle)
        if (at === 0)
            return 1000 - text.length
        if (at > 0)
            return (text[at - 1] === " " ? 800 : 600) - at
        let from = 0
        let gaps = 0
        for (const letter of needle) {
            const next = text.indexOf(letter, from)
            if (next < 0)
                return 0
            gaps += next - from
            from = next + 1
        }
        return Math.max(1, 300 - gaps * 4)
    }
    function show(text) {
        query = text ?? ""
        field.text = query
        list.currentIndex = 0
        open()
        field.forceActiveFocus()
    }
    function accept() {
        const command = matches[list.currentIndex]
        if (!command || command.enabled === false)
            return
        close()
        chosen(command)
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.min(84, Math.round(parent.height * 0.12))
    width: Math.min(560, parent.width - 40)
    height: Math.min(parent.height - y - 24, 54 + Math.min(list.contentHeight, 392) + (matches.length === 0 ? 56 : 8))
    padding: 0
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: "#30000000"
    }
    contentItem: Item {
        Sym {
            x: 16
            y: 17
            name: "search"
            size: 16
            color: Theme.dim
        }
        TextField {
            id: field
            x: 42
            width: parent.width - 56
            height: 50
            color: Theme.text
            font.pixelSize: 16
            placeholderText: "Buscar una orden, una tabla, una consulta…"
            placeholderTextColor: Theme.faint
            selectionColor: Theme.accent
            selectedTextColor: "#ffffff"
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 0
            background: Item {}
            onTextEdited: {
                palette.query = text
                list.currentIndex = 0
            }
            onAccepted: palette.accept()
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Down) {
                    list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
                    event.accepted = true
                } else if (event.key === Qt.Key_Up) {
                    list.currentIndex = Math.max(0, list.currentIndex - 1)
                    event.accepted = true
                } else if (event.key === Qt.Key_PageDown) {
                    list.currentIndex = Math.min(list.count - 1, list.currentIndex + 8)
                    event.accepted = true
                } else if (event.key === Qt.Key_PageUp) {
                    list.currentIndex = Math.max(0, list.currentIndex - 8)
                    event.accepted = true
                }
            }
        }
        Rectangle {
            y: 50
            width: parent.width
            height: 1
            color: Theme.border
        }
        Text {
            y: 70
            width: parent.width
            visible: palette.matches.length === 0
            horizontalAlignment: Text.AlignHCenter
            text: "Nada coincide con «" + palette.query.trim() + "»"
            color: Theme.dim
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        ListView {
            id: list
            x: 6
            y: 55
            width: parent.width - 12
            height: parent.height - 60
            clip: true
            model: palette.matches
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
            section.property: "section"
            section.delegate: Item {
                required property string section
                width: ListView.view.width
                height: 26
                Caption {
                    x: 10
                    y: 9
                    text: parent.section
                }
            }
            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                readonly property bool lit: ListView.isCurrentItem
                readonly property bool usable: modelData.enabled !== false
                width: ListView.view.width
                height: 32
                radius: 7
                color: lit ? Theme.accent : "transparent"
                Sym {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    name: entry.modelData.symbol || "chevron-right"
                    size: 15
                    color: entry.lit ? "#ffffff" : (entry.usable ? Theme.dim : Theme.faint)
                }
                Text {
                    x: 36
                    width: parent.width - 46 - keys.width - 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.modelData.title
                    color: entry.lit ? "#ffffff" : (entry.usable ? Theme.text : Theme.faint)
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                Text {
                    id: keys
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: (entry.modelData.shortcut ?? "").replace(/Ctrl\+/g, "⌃").replace(/Shift\+/g, "⇧").replace(/Alt\+/g, "⌥").replace("Return", "↩")
                    color: entry.lit ? "#e0ffffff" : Theme.faint
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onPositionChanged: list.currentIndex = entry.index
                    onClicked: {
                        list.currentIndex = entry.index
                        palette.accept()
                    }
                }
            }
        }
    }
}
