import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// Left click: forecast. Right click: refresh.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "weather"
    shown: Weather.ready || Weather.loading
    icon: Weather.current ? Weather.icon(Weather.current.code, Weather.current.isDay) : "cloud"
    text: Weather.current ? `${Weather.current.temp}${Weather.unit}` : "…"
    active: panel.visible
    tooltip: Weather.current ? `${Weather.describe(Weather.current.code)} · ${Weather.place}` : ""
    onClicked: panel.toggle()
    onRightClicked: Weather.refresh()

    BarPanel {
        id: panel
        target: root
        title: Weather.place || "Weather"
        icon: "map-pin"
        contentWidth: 330

        headerExtra: IconButton { icon: "refresh-cw"; size: 24; tooltip: "Refresh"; onClicked: Weather.refresh() }

        RowLayout {
            visible: Weather.current !== null
            Layout.fillWidth: true
            spacing: 14
            Icon {
                name: Weather.current ? Weather.icon(Weather.current.code, Weather.current.isDay) : "cloud"
                size: 40
                color: Theme.p.accent
            }
            ColumnLayout {
                spacing: 0
                Label { text: Weather.current ? `${Weather.current.temp}${Weather.unit}` : ""; font.pixelSize: 28; font.weight: Font.DemiBold }
                Label { role: "muted"; text: Weather.current ? `${Weather.describe(Weather.current.code)} · feels ${Weather.current.feels}°` : "" }
            }
            Item { Layout.fillWidth: true }
            ColumnLayout {
                spacing: 2
                RowLayout { Icon { name: "wind"; size: 13 } Label { role: "caption"; text: Weather.current ? `${Weather.current.wind} ${Settings.data.weatherUnits === "imperial" ? "mph" : "km/h"}` : "" } }
                RowLayout { Icon { name: "droplets"; size: 13 } Label { role: "caption"; text: Weather.current ? `${Weather.current.humidity}%` : "" } }
            }
        }

        Item {
            id: hours
            Layout.fillWidth: true
            implicitHeight: 74
            readonly property var slots: Weather.hourly.filter((h, i) => i % 2 === 0).slice(0, 6)
            Repeater {
                model: hours.slots
                delegate: Column {
                    required property var modelData
                    required property int index
                    x: index * hours.width / hours.slots.length
                    width: hours.width / hours.slots.length
                    spacing: 3
                    Label { role: "caption"; text: modelData.hour; anchors.horizontalCenter: parent.horizontalCenter }
                    Icon { name: Weather.icon(modelData.code); anchors.horizontalCenter: parent.horizontalCenter }
                    Label { text: `${modelData.temp}°`; anchors.horizontalCenter: parent.horizontalCenter }
                    Label { role: "caption"; text: modelData.rain > 0 ? `${modelData.rain}%` : " "; color: Theme.p.accent; anchors.horizontalCenter: parent.horizontalCenter }
                }
            }
        }

        Divider {}

        Repeater {
            model: Weather.daily
            delegate: RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Label { Layout.preferredWidth: 60; text: index === 0 ? "Today" : Qt.formatDate(new Date(modelData.day + "T12:00"), "ddd d") }
                Icon { name: Weather.icon(modelData.code) }
                Label { role: "caption"; Layout.preferredWidth: 34; text: modelData.rain > 0 ? `${modelData.rain}%` : ""; color: Theme.p.accent }
                Label { role: "muted"; text: `${modelData.min}°`; Layout.preferredWidth: 30; horizontalAlignment: Text.AlignRight }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 4
                    radius: 2
                    color: Theme.p.track
                    readonly property real lo: Math.min(...Weather.daily.map(d => d.min))
                    readonly property real hi: Math.max(...Weather.daily.map(d => d.max))
                    Rectangle {
                        x: parent.width * (modelData.min - parent.lo) / Math.max(1, parent.hi - parent.lo)
                        width: Math.max(4, parent.width * (modelData.max - modelData.min) / Math.max(1, parent.hi - parent.lo))
                        height: 4
                        radius: 2
                        color: Theme.p.accent
                    }
                }
                Label { text: `${modelData.max}°`; Layout.preferredWidth: 30 }
            }
        }

        Divider {}
        RowLayout {
            Layout.fillWidth: true
            TextBox {
                id: place
                Layout.fillWidth: true
                placeholder: Settings.data.weatherLocation || "City (empty: by IP)"
                onAccepted: { Settings.data.weatherLocation = text.trim(); text = "" }
            }
            IconButton {
                text: Settings.data.weatherUnits === "imperial" ? "°F" : "°C"
                tooltip: "Units"
                onClicked: Settings.data.weatherUnits = Settings.data.weatherUnits === "imperial" ? "metric" : "imperial"
            }
        }
        Label { visible: Weather.error !== ""; role: "caption"; color: Theme.p.red; text: Weather.error }
    }
}
