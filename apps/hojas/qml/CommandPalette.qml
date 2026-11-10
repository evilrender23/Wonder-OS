import QtQuick
import QtQuick.Controls.Basic

/// Everything the app does, a few letters away.
Popup {
    id: palette
    required property var host
    property var found: []

    function show(query) {
        field.text = query === undefined ? "" : query
        refresh()
        open()
        field.forceActiveFocus()
    }
    /// Lower case, without accents.
    function plain(text) {
        return text.normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase()
    }
    /// How well a few letters match a title; 0 when they do not.
    function score(query, title) {
        if (query === "")
            return 1
        const at = title.indexOf(query)
        if (at === 0)
            return 1000
        if (at > 0)
            return title.charAt(at - 1) === " " ? 800 : 600 - at
        // Its letters in order, the closer together the better.
        let from = 0
        let gaps = 0
        for (const letter of query) {
            const next = title.indexOf(letter, from)
            if (next < 0)
                return 0
            gaps += next - from
            from = next + 1
        }
        return Math.max(1, 300 - gaps * 4)
    }
    function refresh() {
        const query = plain(field.text.trim())
        const out = []
        for (const a of host.actions) {
            if (a.id === "palette" || (!host.book.open && !a.always))
                continue
            const s = Math.max(score(query, plain(a.title)), score(query, plain(a.section)) > 500 ? 200 : 0)
            if (s > 0)
                out.push({action: a, score: s})
        }
        // Without a query the registry's order stands, section by section.
        if (query !== "")
            out.sort((a, b) => b.score - a.score)
        found = out.map(o => o.action)
        list.currentIndex = 0
    }
    function accept() {
        if (list.currentIndex < 0 || list.currentIndex >= found.length)
            return
        const id = found[list.currentIndex].id
        close()
        host.run(id)
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.min(86, Math.round(parent.height * 0.12))
    width: Math.min(560, parent.width - 40)
    height: Math.min(parent.height - y - 30, 52 + Math.min(list.contentHeight, 392) + 6)
    padding: 0
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: 13
        color: Theme.menu
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: "#30000000" }

    contentItem: Item {
        Sym {
            x: 17
            y: 18
            name: "search"
            size: 16
            color: Theme.dim
        }
        TextField {
            id: field
            x: 42
            y: 6
            width: parent.width - 56
            height: 40
            placeholderText: "Busca una orden…"
            placeholderTextColor: Theme.faint
            color: Theme.text
            font.pixelSize: 15
            selectionColor: Theme.accent
            selectedTextColor: "#ffffff"
            background: null
            leftPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            onTextEdited: palette.refresh()
            Keys.onDownPressed: list.currentIndex = Math.min(list.count - 1, list.currentIndex + 1)
            Keys.onUpPressed: list.currentIndex = Math.max(0, list.currentIndex - 1)
            Keys.onReturnPressed: palette.accept()
            Keys.onEnterPressed: palette.accept()
        }
        Rectangle {
            y: 51
            width: parent.width
            height: 1
            color: Theme.border
        }
        ListView {
            id: list
            x: 6
            y: 55
            width: parent.width - 12
            height: parent.height - 61
            clip: true
            model: palette.found
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            section.property: field.text.trim() === "" ? "section" : ""
            section.delegate: Item {
                required property string section
                width: list.width
                height: 26
                Text {
                    x: 10
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    text: parent.section
                    color: Theme.dim
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
            }
            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                readonly property bool on: list.currentIndex === index
                width: list.width
                height: 32
                radius: 7
                color: on ? Theme.accent : "transparent"
                Sym {
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    name: entry.modelData.symbol
                    size: 15
                    color: entry.on ? "#ffffff" : Theme.dim
                }
                Text {
                    x: 36
                    width: parent.width - 46 - keys.implicitWidth - (where.visible ? where.implicitWidth + 12 : 0)
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.modelData.title
                    elide: Text.ElideRight
                    color: entry.on ? "#ffffff" : Theme.text
                    font.pixelSize: 13
                }
                Text {
                    id: where
                    visible: field.text.trim() !== ""
                    anchors.right: keys.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.modelData.section
                    color: entry.on ? "#d0ffffff" : Theme.faint
                    font.pixelSize: 11
                }
                Text {
                    id: keys
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        const keys = entry.modelData.shortcut || entry.modelData.hint || ""
                        return keys.replace("Shift", "Mayús").replace("PgDown", "AvPág").replace("PgUp", "RePág").replace(/\+(?=.)/g, " ")
                    }
                    color: entry.on ? "#e0ffffff" : Theme.dim
                    font.pixelSize: 11
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
            Text {
                visible: list.count === 0
                x: 10
                y: 8
                text: "Ninguna orden coincide."
                color: Theme.dim
                font.pixelSize: 13
            }
        }
    }
}
