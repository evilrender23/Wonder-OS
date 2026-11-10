import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Bluetooth
import "../.."
import "../../services"
import "../../components"

// Left click: devices panel. Right click: power toggle.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "bluetooth"
    readonly property var adapter: Bluetooth.defaultAdapter
    readonly property bool powered: adapter !== null && adapter.enabled
    readonly property var devices: adapter ? adapter.devices.values.slice().sort((a, b) => (b.connected - a.connected) || (b.paired - a.paired) || a.name.localeCompare(b.name)) : []
    readonly property var connected: devices.filter(d => d.connected)

    shown: adapter !== null
    icon: !powered ? "bluetooth-off" : connected.length > 0 ? "bluetooth-connected" : "bluetooth"
    text: connected.length === 1 ? connected[0].name : connected.length > 1 ? `${connected.length}` : ""
    active: panel.visible
    tooltip: !powered ? "Bluetooth off" : connected.length ? connected.map(d => d.name).join(", ") : "No devices connected"
    onClicked: panel.toggle()
    onRightClicked: if (adapter) adapter.enabled = !adapter.enabled

    BarPanel {
        id: panel
        target: root
        title: "Bluetooth"
        icon: "bluetooth"
        contentWidth: 320
        onVisibleChanged: if (!visible && root.adapter && root.adapter.discovering) root.adapter.discovering = false

        headerExtra: IconButton {
            visible: root.powered
            icon: "refresh-cw"
            size: 24
            checked: root.adapter && root.adapter.discovering
            tooltip: "Scan for devices"
            onClicked: root.adapter.discovering = !root.adapter.discovering
        }

        Switch {
            text: "Bluetooth"
            detail: root.adapter ? root.adapter.name : ""
            checked: root.powered
            onToggled: v => { if (root.adapter) root.adapter.enabled = v }
        }

        Flickable {
            visible: root.powered
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 280)
            contentHeight: list.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: list
                width: parent.width
                spacing: 2
                Repeater {
                    model: root.devices
                    delegate: ListItem {
                        required property var modelData
                        icon: modelData.connected ? "bluetooth-connected" : "bluetooth"
                        text: modelData.name || modelData.address
                        detail: modelData.state === BluetoothDeviceState.Connecting ? "Connecting…"
                            : modelData.state === BluetoothDeviceState.Disconnecting ? "Disconnecting…"
                            : modelData.pairing ? "Pairing…"
                            : modelData.connected ? (modelData.batteryAvailable ? `Connected · ${Math.round(modelData.battery * 100)}%` : "Connected")
                            : modelData.paired ? "Paired" : "Available"
                        highlighted: modelData.connected
                        actionIcon: modelData.paired ? "trash-2" : ""
                        actionTooltip: "Forget"
                        onClicked: {
                            if (modelData.connected) modelData.disconnect()
                            else if (modelData.paired) modelData.connect()
                            else modelData.pair()
                        }
                        onActionClicked: modelData.forget()
                    }
                }
            }
        }

        Label {
            visible: root.powered && root.devices.length === 0
            role: "muted"
            text: root.adapter && root.adapter.discovering ? "Searching…" : "No devices. Scan to find some."
        }

        IconButton {
            text: "Bluetooth settings"
            icon: "settings"
            onClicked: { panel.close(); Quickshell.execDetached(["sh", "-c", "blueman-manager || ${TERMINAL:-wonderterm} -e bluetoothctl"]) }
        }
    }
}
