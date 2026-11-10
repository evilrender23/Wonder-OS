import QtQuick
import QtQuick.Controls.Basic

/// Find and replace, floating over the corner of the sheet.
Rectangle {
    id: bar
    required property var host
    readonly property var book: host.book
    property bool matchCase: false
    property bool whole: false
    property string message: ""

    function show(text) {
        visible = true
        if (text !== undefined && text !== "")
            what.text = text
        what.forceActiveFocus()
        what.selectAll()
        message = ""
    }
    function hide() {
        visible = false
        host.cancelEdit()
        host.select(host.curRow, host.curCol, false)
    }
    function next(forward) {
        if (what.text === "")
            return
        const found = book.find(what.text, host.curRow, host.curCol, forward, matchCase, whole)
        if (found.found) {
            host.select(found.row, found.col, false)
            message = ""
        } else {
            message = "Sin resultados"
        }
    }
    function replaceOne() {
        if (what.text === "")
            return
        if (!book.replaceAt(host.curRow, host.curCol, what.text, withText.text, matchCase, whole))
            message = ""
        next(true)
    }
    function replaceAllWith(text) {
        withText.text = text
        const count = book.replaceAll(what.text, withText.text, matchCase, whole)
        message = count === 0 ? "Sin resultados" : (count === 1 ? "1 cambio" : count + " cambios")
    }

    visible: false
    width: 372
    height: 82
    radius: 11
    color: Theme.menu
    border.color: Theme.border
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: event => event.accepted = true }

    component Toggle: Rectangle {
        property string label
        property bool on: false
        signal toggled()
        width: 28
        height: 28
        radius: 7
        color: on ? Qt.alpha(Theme.tint, 0.2) : (toggleArea.containsMouse ? Theme.sideSelection : "transparent")
        Text {
            anchors.centerIn: parent
            text: parent.label
            color: parent.on ? Theme.tint : Theme.dim
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        MouseArea { id: toggleArea; anchors.fill: parent; hoverEnabled: true; onClicked: parent.toggled() }
    }

    Field {
        id: what
        x: 10
        y: 10
        width: 190
        placeholderText: "Buscar"
        onAccepted: bar.next(true)
        onTextEdited: bar.message = ""
        Keys.onEscapePressed: bar.hide()
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: bar.message
            color: Theme.dim
            font.pixelSize: 11
        }
    }
    Row {
        x: 206
        y: 10
        spacing: 1
        Toggle { label: "Aa"; on: bar.matchCase; onToggled: bar.matchCase = !bar.matchCase }
        Toggle { label: "[ ]"; on: bar.whole; onToggled: bar.whole = !bar.whole }
        TButton { width: 28; height: 28; symbol: "chevron-up"; onClicked: bar.next(false) }
        TButton { width: 28; height: 28; symbol: "chevron-down"; onClicked: bar.next(true) }
        TButton { width: 28; height: 28; symbol: "x"; tone: Theme.dim; onClicked: bar.hide() }
    }
    Field {
        id: withText
        x: 10
        y: 44
        width: 190
        placeholderText: "Reemplazar por"
        onAccepted: bar.replaceOne()
        Keys.onEscapePressed: bar.hide()
    }
    Row {
        x: 206
        y: 44
        spacing: 6
        DialogButton { implicitWidth: 76; text: "Este"; active: what.text !== ""; onClicked: bar.replaceOne() }
        DialogButton { implicitWidth: 76; text: "Todos"; active: what.text !== ""; onClicked: bar.replaceAllWith(withText.text) }
    }
}
