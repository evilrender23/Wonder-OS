import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// Hidden on machines without a backlight. Scroll: ±5 %.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "brightness"
    shown: Brightness.present
    icon: Brightness.fraction > 0.5 ? "sun" : "sun-dim"
    text: `${Math.round(Brightness.fraction * 100)}%`
    active: panel.visible
    tooltip: "Brightness"
    onClicked: panel.toggle()
    onScrolled: delta => Brightness.set(Brightness.fraction * 100 + (delta > 0 ? 5 : -5))

    BarPanel {
        id: panel
        target: root
        title: "Brightness"
        icon: "sun"
        contentWidth: 260

        RowLayout {
            Layout.fillWidth: true
            Icon { name: "sun-dim" }
            ThinSlider {
                id: slider
                Layout.fillWidth: true
                from: 1; to: 100; stepSize: 1
                onMoved: Brightness.set(value)
            }
            Binding { target: slider; property: "value"; value: Brightness.fraction * 100; when: !slider.pressed; restoreMode: Binding.RestoreNone }
            Label { text: `${Math.round(slider.value)}%`; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignRight }
        }
        Label { visible: !Brightness.canSet; role: "caption"; text: "Install brightnessctl to change it." }
    }
}
