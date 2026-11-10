import QtQuick

/// Find, and under it replace, floating over the corner of the sheets.
Rectangle {
    id: bar
    readonly property var editor: App.editor
    property bool replacing: false
    property bool caseSensitive: false
    property bool wholeWords: false
    readonly property alias query: find.text
    signal closed()

    function show(withReplace, text) {
        replacing = withReplace
        visible = true
        if (text !== undefined && text !== "")
            find.text = text
        find.forceActiveFocus()
        find.selectAll()
        search()
    }
    function hide() {
        visible = false
        editor.setFind("", false, false)
        closed()
    }
    function search() {
        editor.setFind(find.text, caseSensitive, wholeWords)
    }

    visible: false
    width: 372
    height: replacing ? 76 : 40
    radius: 10
    color: Theme.pill
    border.color: Theme.border

    Row {
        x: 6
        y: 6
        spacing: 2
        Field {
            id: find
            width: 178
            placeholderText: "Buscar"
            rightPadding: count.implicitWidth + 14
            onTextEdited: bar.search()
            onAccepted: bar.editor.findNext(true)
            Keys.onEscapePressed: bar.hide()
            Text {
                id: count
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: find.text !== ""
                text: bar.editor.matchCount === 0 ? "Nada" : (bar.editor.matchIndex > 0 ? bar.editor.matchIndex + " de " : "") + bar.editor.matchCount
                color: Theme.faint
                font.pixelSize: 12
            }
        }
        Item { width: 4; height: 1 }
        TButton {
            y: 1
            symbol: "case-sensitive"
            checked: bar.caseSensitive
            onClicked: {
                bar.caseSensitive = !bar.caseSensitive
                bar.search()
            }
        }
        TButton {
            y: 1
            symbol: "whole-word"
            checked: bar.wholeWords
            onClicked: {
                bar.wholeWords = !bar.wholeWords
                bar.search()
            }
        }
        TButton {
            y: 1
            symbol: "chevron-up"
            active: bar.editor.matchCount > 0
            onClicked: bar.editor.findNext(false)
        }
        TButton {
            y: 1
            symbol: "chevron-down"
            active: bar.editor.matchCount > 0
            onClicked: bar.editor.findNext(true)
        }
        TButton {
            y: 1
            symbol: "x"
            onClicked: bar.hide()
        }
    }
    Row {
        x: 6
        y: 42
        visible: bar.replacing
        spacing: 6
        Field {
            id: replacement
            width: 178
            placeholderText: "Reemplazar por"
            onAccepted: bar.editor.replaceOne(text)
            Keys.onEscapePressed: bar.hide()
        }
        DialogButton {
            implicitWidth: 92
            text: "Reemplazar"
            active: bar.editor.matchCount > 0
            onClicked: bar.editor.replaceOne(replacement.text)
        }
        DialogButton {
            implicitWidth: 78
            text: "Todos"
            active: bar.editor.matchCount > 0
            onClicked: bar.editor.replaceAll(replacement.text)
        }
    }
}
