import QtQuick
import QtQuick.Controls.Basic

/// The sidebar: the document's headings, alive as it is written, and its numbers.
Rectangle {
    id: panel
    readonly property var editor: App.editor
    /// The heading the cursor is under.
    readonly property int current: {
        let found = -1
        const outline = editor.outline
        for (let i = 0; i < outline.length; ++i) {
            if (outline[i].position <= editor.position)
                found = i
        }
        return found
    }
    property int goal: 0
    signal jump(int position)
    color: Theme.sidebar

    SectionLabel {
        id: heading
        x: 16
        y: 14
        text: "Esquema"
    }
    ListView {
        id: list
        anchors.top: heading.bottom
        anchors.topMargin: 8
        anchors.bottom: facts.top
        anchors.bottomMargin: 8
        width: parent.width
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: panel.editor.outline
        ScrollBar.vertical: ScrollBar {}
        delegate: Item {
            id: row
            required property var modelData
            required property int index
            width: list.width
            height: 28
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                radius: 6
                color: panel.current === row.index ? Theme.sideSelection : (over.containsMouse ? Theme.hover : "transparent")
            }
            Text {
                x: 16 + Math.max(0, row.modelData.level - 1) * 12
                width: parent.width - x - 40
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.text
                elide: Text.ElideRight
                color: row.modelData.level <= 1 ? Theme.text : Theme.dim
                font.pixelSize: 13
                font.weight: row.modelData.level === 0 ? Font.DemiBold : (row.modelData.level === 1 ? Font.Medium : Font.Normal)
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.page
                color: Theme.faint
                font.pixelSize: 12
            }
            MouseArea {
                id: over
                anchors.fill: parent
                hoverEnabled: true
                onClicked: panel.jump(row.modelData.position)
            }
        }
        Text {
            visible: list.count === 0
            x: 16
            width: parent.width - 32
            text: "Los títulos y encabezados del documento aparecerán aquí. Escribe «# » al empezar una línea para hacer uno."
            wrapMode: Text.Wrap
            lineHeight: 1.25
            color: Theme.dim
            font.pixelSize: 12
        }
    }
    Column {
        id: facts
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        x: 16
        width: parent.width - 32
        spacing: 5
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
        Item { width: 1; height: 3 }
        Repeater {
            model: [
                {label: "Palabras", value: panel.editor.counts.words},
                {label: "Caracteres", value: panel.editor.counts.characters},
                {label: "Párrafos", value: panel.editor.counts.paragraphs},
                {label: "Páginas", value: panel.editor.pageCount}
            ]
            Item {
                required property var modelData
                width: facts.width
                height: 17
                Text {
                    text: parent.modelData.label
                    color: Theme.dim
                    font.pixelSize: 12
                }
                Text {
                    anchors.right: parent.right
                    text: Number(parent.modelData.value ?? 0).toLocaleString(Qt.locale("es_ES"), "f", 0)
                    color: Theme.text
                    font.pixelSize: 12
                }
            }
        }
    }
}
