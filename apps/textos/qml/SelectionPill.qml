import QtQuick

/// The few things most likely wanted of what has just been selected.
Rectangle {
    id: pill
    readonly property var look: App.editor.format
    signal act(string id)
    width: row.width + 10
    height: 34
    radius: 10
    color: Theme.pill
    border.color: Theme.border

    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.topMargin: 3
        anchors.bottomMargin: -3
        radius: 10
        color: "#22000000"
    }
    Row {
        id: row
        x: 5
        y: 4
        spacing: 1
        TButton {
            symbol: "bold"
            checked: pill.look.bold
            onClicked: pill.act("bold")
        }
        TButton {
            symbol: "italic"
            checked: pill.look.italic
            onClicked: pill.act("italic")
        }
        TButton {
            symbol: "underline"
            checked: pill.look.underline
            onClicked: pill.act("underline")
        }
        TButton {
            symbol: "strikethrough"
            checked: pill.look.strike
            onClicked: pill.act("strike")
        }
        Rectangle {
            y: 5
            width: 1
            height: 16
            color: Theme.border
        }
        TButton {
            symbol: "highlighter"
            checked: pill.look.highlight !== ""
            onClicked: pill.act("highlight")
        }
        TButton {
            symbol: "link"
            checked: pill.look.link !== ""
            onClicked: pill.act("link")
        }
        Rectangle {
            y: 5
            width: 1
            height: 16
            color: Theme.border
        }
        TButton {
            symbol: "heading-1"
            checked: pill.look.style === "h1"
            onClicked: pill.act(pill.look.style === "h1" ? "styleBody" : "styleH1")
        }
        TButton {
            symbol: "text-quote"
            checked: pill.look.style === "quote"
            onClicked: pill.act(pill.look.style === "quote" ? "styleBody" : "styleQuote")
        }
    }
}
