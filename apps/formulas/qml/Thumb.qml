import QtQuick

/// A formula small enough for the room it is given: in a list, on a card.
Item {
    id: thumb
    property alias latex: formula.latex
    property alias size: formula.size
    property color color: Theme.text
    clip: true
    FormulaItem {
        id: formula
        anchors.centerIn: parent
        color: thumb.color
        size: 16
        fit: thumb.width
        // Too tall still: the whole picture shrinks.
        scale: implicitHeight > thumb.height && thumb.height > 0 ? thumb.height / implicitHeight : 1
    }
}
