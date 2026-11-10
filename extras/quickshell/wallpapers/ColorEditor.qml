import QtQuick
import QtQuick.Layouts
import "../services"
import "../components"
import ".."

// Colour field: a swatch and the hex value; clicking the swatch unfolds hue,
// saturation and brightness sliders. Emits `picked("#rrggbb")`.
ColumnLayout {
    id: root

    property color color: "#000000"
    property bool expanded: false
    signal picked(string hex)

    spacing: 6

    function hex(c) {
        const h = v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, "0")
        return "#" + h(c.r) + h(c.g) + h(c.b)
    }
    function fromHsv(h, s, v) { return hex(Qt.hsva(h, s, v, 1)) }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Rectangle {
            width: 30; height: 22; radius: 6
            color: root.color
            border.width: 1
            border.color: Theme.p.stroke1
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.expanded = !root.expanded }
        }
        TextBox {
            id: field
            Layout.fillWidth: true
            implicitHeight: 24
            text: root.hex(root.color)
            onAccepted: {
                const t = text.trim().replace(/^#?/, "#")
                if (/^#[0-9a-fA-F]{6}$/.test(t)) root.picked(t.toLowerCase())
                else text = root.hex(root.color)
            }
            Connections { target: root; function onColorChanged() { field.text = root.hex(root.color) } }
        }
        Icon {
            name: root.expanded ? "chevron-down" : "chevron-right"
            size: 14
            color: Theme.p.text3
            MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: root.expanded = !root.expanded }
        }
    }

    // Hue / saturation / brightness. The hue of a grey is undefined (-1):
    // keep the last one so dragging saturation up from grey isn't stuck red.
    property real lastHue: 0
    readonly property real hue: root.color.hsvHue >= 0 ? root.color.hsvHue : lastHue
    onHueChanged: if (root.color.hsvHue >= 0) lastHue = root.color.hsvHue

    GridLayout {
        Layout.fillWidth: true
        visible: root.expanded
        columns: 2
        columnSpacing: 8
        rowSpacing: 2

        Text { text: I18n.tr("Tono"); font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.text3 }
        ThinSlider {
            id: hueSlider
            Layout.fillWidth: true
            from: 0; to: 1
            onMoved: { root.lastHue = value; root.picked(root.fromHsv(value, root.color.hsvSaturation, root.color.hsvValue)) }
            Binding on value { when: !hueSlider.pressed; value: root.hue }
        }
        Text { text: I18n.tr("Saturación"); font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.text3 }
        ThinSlider {
            id: satSlider
            Layout.fillWidth: true
            from: 0; to: 1
            onMoved: root.picked(root.fromHsv(root.hue, value, root.color.hsvValue))
            Binding on value { when: !satSlider.pressed; value: root.color.hsvSaturation }
        }
        Text { text: I18n.tr("Brillo"); font.family: Theme.fontFamily; font.pixelSize: 11; color: Theme.p.text3 }
        ThinSlider {
            id: valSlider
            Layout.fillWidth: true
            from: 0; to: 1
            onMoved: root.picked(root.fromHsv(root.hue, root.color.hsvSaturation, value))
            Binding on value { when: !valSlider.pressed; value: root.color.hsvValue }
        }
    }
}
