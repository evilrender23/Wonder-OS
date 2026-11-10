import QtQuick
import QtQuick.Controls.Basic

/// Where the LaTeX is written: coloured as it is typed, with the pair of
/// each brace in sight and what may follow a backslash a key away.
Rectangle {
    id: editor
    /// What may follow the backslash being typed.
    property var found: []
    property int foundAt: 0
    property int prefixLength: 0
    property bool typed: false
    readonly property bool completing: found.length > 0
    readonly property alias area: area
    readonly property bool selecting: area.selectedText !== ""
    /// Where the selection starts, in the editor's coordinates: the pill goes by it.
    readonly property rect selectionRect: {
        area.selectionStart
        area.selectionEnd
        flick.contentY
        const r = area.positionToRectangle(area.selectionStart)
        const e = area.positionToRectangle(area.selectionEnd)
        const p = area.mapToItem(editor, r.x, r.y)
        const sameLine = Math.abs(e.y - r.y) < 2
        return Qt.rect(p.x, p.y, sameLine ? e.x - r.x : area.width - r.x, r.height)
    }
    signal undoRequested()
    signal redoRequested()

    color: Theme.window

    /// Where the cursor lands in a snippet: its `|`.
    function marker(snippet) {
        let at = snippet.indexOf("{|}")
        if (at >= 0)
            return at + 1
        at = snippet.indexOf("|}")
        if (at >= 0)
            return at
        at = snippet.indexOf(" | ")
        if (at >= 0)
            return at + 1
        return snippet.endsWith(" |") ? snippet.length - 1 : -1
    }
    /// Puts a snippet where the cursor is; what was selected goes where its `|` is.
    function insert(snippet) {
        const from = area.selectionStart
        const chosen = area.selectedText
        const at = marker(snippet)
        const text = at < 0 ? snippet : snippet.slice(0, at) + chosen + snippet.slice(at + 1)
        App.breakRun()
        area.remove(area.selectionStart, area.selectionEnd)
        area.insert(from, text)
        area.cursorPosition = at < 0 ? from + text.length : from + at + chosen.length
        App.breakRun()
        area.forceActiveFocus()
    }
    function goTo(position) {
        area.cursorPosition = Math.max(0, Math.min(area.length, position))
        area.forceActiveFocus()
    }
    function refreshCompletions() {
        if (!typed) {
            found = []
            return
        }
        typed = false
        const match = /\\([a-zA-Z]*)$/.exec(area.text.slice(0, area.cursorPosition))
        if (!match) {
            found = []
            return
        }
        const list = App.completions(match[1])
        // Nothing to offer when the one match is what is already written.
        found = list.length === 1 && list[0].command === match[0] ? [] : list
        foundAt = 0
        prefixLength = match[0].length
    }
    function complete() {
        if (!completing)
            return
        const item = found[foundAt]
        const end = area.cursorPosition
        found = []
        area.select(end - prefixLength, end)
        insert(item.insert)
    }
    /// Offers what may follow, wherever the cursor is.
    function offer() {
        typed = true
        refreshCompletions()
    }

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }
        TextArea.flickable: TextArea {
            id: area
            property bool syncing: false
            leftPadding: 18
            rightPadding: 18
            topPadding: 14
            bottomPadding: 14
            color: Theme.text
            font.family: "monospace"
            font.pixelSize: 15
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            persistentSelection: true
            selectionColor: Theme.dark ? "#555b5bd6" : "#405b5bd6"
            selectedTextColor: Theme.text
            placeholderText: "Escribe LaTeX: \\frac{a}{b}, x^2, \\sqrt{x}… «\\» propone órdenes."
            placeholderTextColor: Theme.faint
            background: null
            tabStopDistance: 30
            Component.onCompleted: {
                text = App.latex
                cursorPosition = length
            }
            onTextChanged: {
                // Colouring the text again says it changed, with nothing typed.
                if (syncing || marks.painting || text === App.latex)
                    return
                App.latex = text
                editor.typed = true
                Qt.callLater(editor.refreshCompletions)
                runTimer.restart()
            }
            onCursorPositionChanged: Qt.callLater(editor.refreshCompletions)
            onActiveFocusChanged: if (!activeFocus && !App.testing) editor.found = []
            Keys.onPressed: event => {
                const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                if (editor.completing) {
                    if (event.key === Qt.Key_Down) {
                        editor.foundAt = (editor.foundAt + 1) % editor.found.length
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up) {
                        editor.foundAt = (editor.foundAt + editor.found.length - 1) % editor.found.length
                        event.accepted = true
                    } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        editor.complete()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        editor.found = []
                        event.accepted = true
                    }
                    if (event.accepted)
                        return
                }
                // The document's history, not the field's: it also undoes what the panels did.
                if (ctrl && event.key === Qt.Key_Z) {
                    if (shift)
                        editor.redoRequested()
                    else
                        editor.undoRequested()
                    event.accepted = true
                } else if (ctrl && event.key === Qt.Key_Y) {
                    editor.redoRequested()
                    event.accepted = true
                } else if (event.text === "{" && !ctrl) {
                    // A brace brings its pair; around a selection, both.
                    const from = selectionStart
                    const chosen = selectedText
                    remove(selectionStart, selectionEnd)
                    insert(from, "{" + chosen + "}")
                    cursorPosition = from + 1 + chosen.length
                    event.accepted = true
                } else if (event.text === "}" && !ctrl && selectedText === "" && text.charAt(cursorPosition) === "}") {
                    cursorPosition += 1
                    event.accepted = true
                }
            }
        }
    }
    Highlighter {
        id: marks
        document: area.textDocument
        dark: Theme.dark
        cursor: area.cursorPosition
        errors: App.errors
    }
    Connections {
        target: App
        function onChanged() {
            if (area.text === App.latex)
                return
            area.syncing = true
            const at = area.cursorPosition
            const whole = at >= area.length
            area.text = App.latex
            area.cursorPosition = whole ? area.length : Math.min(at, area.length)
            area.syncing = false
            editor.found = []
        }
    }
    // A pause in the typing ends the step that one undo takes back.
    Timer {
        id: runTimer
        interval: 900
        onTriggered: App.breakRun()
    }

    // What may follow the backslash.
    Rectangle {
        id: offers
        readonly property rect at: {
            flick.contentY
            const r = area.cursorRectangle
            const p = area.mapToItem(editor, r.x, r.y)
            return Qt.rect(p.x, p.y, r.width, r.height)
        }
        readonly property bool above: at.y + at.height + height + 6 > editor.height && at.y - height - 4 > -editor.y
        visible: editor.completing
        x: Math.max(8, Math.min(editor.width - width - 8, at.x - 12))
        y: above ? at.y - height - 4 : at.y + at.height + 4
        z: 5
        width: 300
        height: editor.found.length * 30 + 10
        radius: 9
        color: Theme.menu
        border.color: Theme.border
        Column {
            x: 5
            y: 5
            width: parent.width - 10
            Repeater {
                model: editor.found
                Rectangle {
                    id: offer
                    required property var modelData
                    required property int index
                    readonly property bool on: editor.foundAt === index
                    width: parent.width
                    height: 30
                    radius: 6
                    color: on ? Theme.accent : "transparent"
                    Thumb {
                        x: 4
                        width: 40
                        height: 26
                        anchors.verticalCenter: parent.verticalCenter
                        latex: offer.modelData.latex ?? ""
                        size: 15
                        color: offer.on ? "#ffffff" : Theme.text
                    }
                    Text {
                        x: 50
                        anchors.verticalCenter: parent.verticalCenter
                        text: offer.modelData.command
                        color: offer.on ? "#ffffff" : Theme.text
                        font.family: "monospace"
                        font.pixelSize: 13
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, 130)
                        elide: Text.ElideRight
                        text: offer.modelData.title ?? ""
                        color: offer.on ? "#d8ffffff" : Theme.dim
                        font.pixelSize: 11
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            editor.foundAt = offer.index
                            editor.complete()
                        }
                    }
                }
            }
        }
    }
}
