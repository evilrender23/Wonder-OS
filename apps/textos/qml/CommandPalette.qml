import QtQuick
import QtQuick.Controls.Basic

/// Everything Textos can do, a few letters away.
Popup {
    id: commands
    /// [{id, title, section, shortcut, symbol, run, enabled}]: the window's register of actions.
    property var actions: []
    signal chosen(string id)

    function show(query) {
        field.text = query ?? ""
        refill()
        open()
    }
    /// The letters of `query` in order somewhere in `text`; a run of them together counts for more.
    function matches(text, query) {
        if (query === "")
            return 1
        if (text.includes(query))
            return 2
        let at = 0
        for (const letter of query) {
            at = text.indexOf(letter, at)
            if (at < 0)
                return 0
            ++at
        }
        return 1
    }
    function refill() {
        const query = App.fold(field.text.trim())
        const found = []
        let best = 0
        for (const action of actions) {
            if (action.hidden)
                continue
            const score = matches(App.fold(action.title + " " + action.section), query)
            if (score > 0)
                found.push({action, score})
            best = Math.max(best, score)
        }
        rows.clear()
        for (const one of found) {
            // Loose matches only when nothing matches properly.
            if (one.score < best)
                continue
            const action = one.action
            rows.append({
                actionId: action.id,
                title: action.title,
                section: action.section,
                shortcut: action.shortcut ?? "",
                symbol: action.symbol ?? "",
                usable: action.enabled ? action.enabled() : true
            })
        }
        list.currentIndex = 0
        list.positionViewAtBeginning()
    }
    function pretty(shortcut) {
        return shortcut.replace(/Ctrl\+/g, "Ctrl ").replace(/Shift\+/g, "⇧ ").replace(/Alt\+/g, "Alt ").replace("Return", "↵")
    }
    function accept() {
        if (list.currentIndex < 0 || list.currentIndex >= rows.count)
            return
        const row = rows.get(list.currentIndex)
        if (!row.usable)
            return
        const id = row.actionId
        close()
        chosen(id)
    }

    modal: true
    x: Math.round((parent.width - width) / 2)
    y: 64
    width: Math.min(560, parent.width - 40)
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: field.forceActiveFocus()
    background: Rectangle {
        radius: 12
        color: Theme.pill
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: "#30000000"
    }
    ListModel {
        id: rows
    }
    contentItem: Column {
        Item {
            width: parent.width
            height: 46
            Sym {
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: 16
                color: Theme.dim
            }
            TextField {
                id: field
                anchors.fill: parent
                anchors.leftMargin: 42
                anchors.rightMargin: 14
                color: Theme.text
                font.pixelSize: 15
                placeholderText: "Buscar una orden…"
                placeholderTextColor: Theme.faint
                selectionColor: Theme.accent
                selectedTextColor: "#ffffff"
                verticalAlignment: TextInput.AlignVCenter
                background: null
                onTextEdited: commands.refill()
                onAccepted: commands.accept()
                Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
                Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
            }
        }
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.line
        }
        ListView {
            id: list
            width: parent.width
            height: Math.min(contentHeight + 12, Math.max(120, (commands.parent ? commands.parent.height : 600) - 220), 384)
            topMargin: 6
            bottomMargin: 6
            clip: true
            model: rows
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            section.property: "section"
            section.delegate: Item {
                required property string section
                width: list.width
                height: 26
                SectionLabel {
                    x: 16
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    text: parent.section
                }
            }
            delegate: Item {
                id: row
                required property int index
                required property string title
                required property string shortcut
                required property string symbol
                required property bool usable
                width: list.width
                height: 32
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    radius: 7
                    color: list.currentIndex === row.index ? Theme.accent : "transparent"
                }
                Sym {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.symbol
                    size: 15
                    color: list.currentIndex === row.index ? "#ffffff" : (row.usable ? Theme.dim : Theme.faint)
                }
                Text {
                    x: 42
                    width: parent.width - x - keys.implicitWidth - 30
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.title
                    elide: Text.ElideRight
                    color: list.currentIndex === row.index ? "#ffffff" : (row.usable ? Theme.text : Theme.faint)
                    font.pixelSize: 13
                }
                Text {
                    id: keys
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: commands.pretty(row.shortcut)
                    color: list.currentIndex === row.index ? "#d9ffffff" : Theme.faint
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onPositionChanged: list.currentIndex = row.index
                    onClicked: {
                        list.currentIndex = row.index
                        commands.accept()
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "Ninguna orden se llama así"
                color: Theme.dim
                font.pixelSize: 13
            }
        }
    }
}
