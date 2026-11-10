import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// The date and time, as on macOS; clicking opens Notification Centre
// (notifications, calendar and weather). Right click: 12/24 h. Middle
// click: timezone list.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "clock"
    icon: Notifs.dnd ? "moon" : ""
    iconColor: Theme.p.text
    text: Time.format(clock.date, Settings.data.use24h ? "ddd d MMM  HH:mm" : "ddd d MMM  h:mm AP")
    active: panel.visible
    tooltip: Settings.data.timezone || "Local time"
    onClicked: panel.toggle()
    onRightClicked: Settings.data.use24h = !Settings.data.use24h
    onMiddleClicked: zones.toggle()

    SystemClock { id: clock; precision: SystemClock.Minutes }

    BarPanel {
        id: panel
        target: root
        contentWidth: 340
        padding: 10

        // Notifications
        RowLayout {
            Layout.fillWidth: true
            Label { role: "section"; text: "Notificaciones"; Layout.fillWidth: true }
            IconButton { visible: Notifs.count > 0; icon: "trash-2"; size: 22; tooltip: "Borrar todas"; onClicked: Notifs.clearAll() }
            IconButton {
                icon: Notifs.dnd ? "bell-off" : "bell"
                size: 22
                checked: Notifs.dnd
                tooltip: Notifs.dnd ? "No molestar: activado" : "No molestar"
                onClicked: Settings.data.doNotDisturb = !Settings.data.doNotDisturb
            }
        }
        Flickable {
            visible: Notifs.count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 360)
            contentHeight: list.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: list
                width: parent.width
                spacing: 6
                Repeater {
                    model: Notifs.history
                    delegate: NotificationCard { required property var modelData; entry: modelData; Layout.fillWidth: true }
                }
            }
        }
        Label { visible: Notifs.count === 0; role: "muted"; text: "No hay notificaciones"; Layout.alignment: Qt.AlignHCenter }

        // Calendar
        ControlTile {
            Label { role: "title"; text: Time.format(clock.date, "dddd, d 'de' MMMM") }
            MonthView { Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Label { role: "caption"; text: Settings.data.timezone || "Hora local"; Layout.fillWidth: true }
                IconButton { icon: "map-pin"; size: 22; tooltip: "Zona horaria"; onClicked: { panel.close(); zones.open() } }
                IconButton {
                    text: Settings.data.use24h ? "24h" : "12h"
                    size: 22
                    onClicked: Settings.data.use24h = !Settings.data.use24h
                }
            }
        }

        // Weather
        ControlTile {
            visible: Weather.current !== null
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Icon { name: Weather.current ? Weather.icon(Weather.current.code, Weather.current.isDay) : "cloud"; size: 28; color: Theme.p.accent }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label { text: Weather.place || "Tiempo"; font.weight: Font.DemiBold }
                    Label { role: "muted"; text: Weather.current ? Weather.describe(Weather.current.code) : "" }
                }
                Label { text: Weather.current ? `${Weather.current.temp}${Weather.unit}` : ""; font.pixelSize: 22; font.weight: Font.DemiBold }
            }
        }
    }

    BarPanel {
        id: zones
        target: root
        title: "Timezone"
        icon: "map-pin"
        contentWidth: 260

        Repeater {
            model: [""].concat(Time.zones)
            delegate: ListItem {
                required property string modelData
                text: modelData || "Local time"
                trailing: Time.format(clock.date, "HH:mm", modelData)
                highlighted: Settings.data.timezone === modelData
                onClicked: { Settings.data.timezone = modelData; zones.close() }
            }
        }
    }
}
