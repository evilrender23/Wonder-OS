import QtQuick
import QtQuick.Controls.Basic

/// Every order of the app, found by typing a few letters of its name.
Popup {
    id: commands
    /// [{id, title, section, keys, symbol, run}]
    property var actions: []
    signal chosen(string id)
    property var found: []

    /// Lower case and without accents, so that «titulo» finds «Título».
    function plain(text) {
        return String(text).normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase()
    }
    /// How well `query` fits `text`: 0 for not at all; whole words and early letters count more.
    function score(query, text) {
        if (query === "")
            return 1
        const at = text.indexOf(query)
        if (at >= 0)
            return 1000 - at - (at > 0 && text[at - 1] !== " " ? 100 : 0)
        let from = 0
        let gaps = 0
        for (const letter of query) {
            const next = text.indexOf(letter, from)
            if (next < 0)
                return 0
            gaps += next - from
            from = next + 1
        }
        return Math.max(1, 500 - gaps)
    }
    function search(query) {
        const wanted = plain(query).trim()
        const hits = []
        for (const action of actions) {
            if (action.hidden)
                continue
            const points = Math.max(score(wanted, plain(action.title)), score(wanted, plain(action.section + " " + action.title)) - 200)
            if (points > 0)
                hits.push({action: action, points: points})
        }
        // Typed: the best first. Empty: as the registry has them, by section.
        if (wanted !== "")
            hits.sort((a, b) => b.points - a.points)
        found = hits.map(hit => hit.action)
        list.currentIndex = 0
    }
    function show(query) {
        field.text = query || ""
        search(field.text)
        open()
        field.forceActiveFocus()
    }
    function accept() {
        const action = found[list.currentIndex]
        close()
        if (action)
            chosen(action.id)
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.min(90, Math.round(parent.height * 0.12))
    width: Math.min(560, parent.width - 40)
    padding: 0
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: "#30000000" }
    background: Rectangle {
        radius: 12
        color: Theme.menu
        border.color: Theme.border
    }
    contentItem: Column {
        Item {
            width: parent.width
            height: 48
            Sym {
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 16
                color: Theme.dim
            }
            TextInput {
                id: field
                anchors.fill: parent
                anchors.leftMargin: 42
                anchors.rightMargin: 16
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.text
                font.pixelSize: 16
                selectionColor: Theme.accent
                selectedTextColor: "#ffffff"
                clip: true
                onTextEdited: commands.search(text)
                Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
                Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
                Keys.onReturnPressed: commands.accept()
                Keys.onEnterPressed: commands.accept()
                Text {
                    visible: field.text === ""
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Buscar una orden…"
                    color: Theme.faint
                    font.pixelSize: 16
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.border }
        ListView {
            id: list
            width: parent.width
            height: Math.min(contentHeight + 10, Math.max(120, commands.parent.height - commands.y - 110), 420)
            topMargin: 5
            bottomMargin: 5
            clip: true
            model: commands.found
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar { width: 8 }
            delegate: Item {
                id: entry
                required property var modelData
                required property int index
                readonly property bool first: field.text.trim() === "" && (index === 0 || ((commands.found || [])[index - 1] || {}).section !== modelData.section)
                width: list.width
                height: first ? 54 : 30
                Text {
                    visible: entry.first
                    x: 16
                    y: 8
                    text: entry.modelData.section
                    color: Theme.dim
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.4
                }
                Rectangle {
                    x: 6
                    y: parent.height - 30
                    width: parent.width - 12
                    height: 30
                    radius: 6
                    color: list.currentIndex === entry.index ? Theme.accent : (rowArea.containsMouse ? Theme.hover : "transparent")
                    readonly property color ink: list.currentIndex === entry.index ? "#ffffff" : Theme.text
                    Sym {
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        name: entry.modelData.symbol || "chevron-right"
                        size: 15
                        color: parent.ink
                    }
                    Text {
                        x: 36
                        width: parent.width - 46 - keys.width - (section.visible ? section.width + 10 : 0)
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modelData.title
                        color: parent.ink
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Text {
                        id: section
                        visible: field.text.trim() !== ""
                        anchors.right: keys.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modelData.section
                        color: list.currentIndex === entry.index ? "#ccffffff" : Theme.faint
                        font.pixelSize: 12
                    }
                    Text {
                        id: keys
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: Theme.keys(entry.modelData.keys)
                        color: list.currentIndex === entry.index ? "#ccffffff" : Theme.dim
                        font.pixelSize: 12
                    }
                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            list.currentIndex = entry.index
                            commands.accept()
                        }
                    }
                }
            }
            Text {
                visible: list.count === 0
                anchors.centerIn: parent
                text: "Ninguna orden coincide"
                color: Theme.dim
                font.pixelSize: 13
            }
        }
    }
}
