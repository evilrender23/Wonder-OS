import QtQuick

/// The colours of the theme, a few of every day, none, and any other by its code.
Column {
    id: well
    /// What is set: a token of the theme («@accent1»), «#rrggbb» or nothing.
    property string value
    /// The colour it comes to.
    property string shown
    property bool allowNone: true
    /// [{token, color, name}]
    property var swatches: []
    signal picked(string color)
    readonly property var plain: ["#ffffff", "#d1d1d6", "#8e8e93", "#1d1d1f", "#ff3b30", "#ff9f0a", "#ffcc00", "#34c759", "#0a84ff", "#af52de"]
    /// Ten to a row, whatever the width.
    readonly property int cell: Math.max(14, Math.min(22, Math.floor((width - 9 * 5) / 10)))
    width: parent ? parent.width : 0
    spacing: 6

    Flow {
        width: parent.width
        spacing: 5
        Rectangle {
            visible: well.allowNone
            width: well.cell
            height: well.cell
            radius: well.cell / 2
            color: "transparent"
            border.color: well.value === "" ? Theme.tint : Theme.border
            border.width: well.value === "" ? 2 : 1
            Sym { anchors.centerIn: parent; name: "slash"; size: 12; color: Theme.dim }
            MouseArea { anchors.fill: parent; onClicked: well.picked("") }
        }
        Repeater {
            model: well.swatches
            Rectangle {
                required property var modelData
                width: well.cell
                height: well.cell
                radius: well.cell / 2
                color: modelData.color
                border.color: well.value === modelData.token ? Theme.tint : Theme.swatch
                border.width: well.value === modelData.token ? 2 : 1
                MouseArea { anchors.fill: parent; onClicked: well.picked(parent.modelData.token) }
            }
        }
    }
    Flow {
        width: parent.width
        spacing: 5
        Repeater {
            // As many as the row above, which is one short when «none» cannot be chosen.
            model: well.allowNone ? well.plain : well.plain.filter(c => c !== "#d1d1d6")
            Rectangle {
                required property string modelData
                width: well.cell
                height: well.cell
                radius: well.cell / 2
                color: modelData
                border.color: well.value.toLowerCase() === modelData ? Theme.tint : Theme.swatch
                border.width: well.value.toLowerCase() === modelData ? 2 : 1
                MouseArea { anchors.fill: parent; onClicked: well.picked(parent.modelData) }
            }
        }
    }
    Row {
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            height: 22
            radius: 5
            color: well.shown === "" ? "transparent" : well.shown
            border.color: Theme.border
        }
        Field {
            id: code
            width: 96
            placeholderText: "#rrggbb"
            text: well.shown
            wrong: text !== "" && !/^#[0-9a-fA-F]{6}$/.test(text)
            onEditingFinished: if (/^#[0-9a-fA-F]{6}$/.test(text) && text.toLowerCase() !== well.shown.toLowerCase()) well.picked(text.toLowerCase())
        }
    }
}
