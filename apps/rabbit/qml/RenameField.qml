import QtQuick
import QtQuick.Controls.Basic

/// The name being edited in place. Opens with the name selected up to its extension.
TextField {
    id: field
    property string original
    property bool isDir: false
    signal done(string name)
    signal cancelled()
    property bool finished: false

    text: original
    font.pixelSize: 12
    padding: 2
    leftPadding: 4
    rightPadding: 4
    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: "white"
    background: Rectangle {
        radius: 4
        color: Theme.window
        border.color: Theme.accent
        border.width: 2
    }
    Component.onCompleted: {
        forceActiveFocus()
        const dot = isDir ? -1 : original.lastIndexOf(".")
        select(0, dot > 0 ? dot : original.length)
    }
    function finish(accept) {
        if (finished)
            return
        finished = true
        if (accept && text !== "" && text !== original)
            done(text)
        else
            cancelled()
    }
    onAccepted: finish(true)
    onActiveFocusChanged: if (!activeFocus) finish(true)
    Keys.onEscapePressed: finish(false)
}
