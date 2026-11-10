import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Services.UPower
import "../.."
import "../../services"
import "../../components"

// Battery and power profile. Hidden on a desktop without a battery (the
// power profile is in Control Centre there). Right click cycles the profile.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "battery"
    readonly property var device: UPower.displayDevice
    readonly property bool hasBattery: device !== null && device.isLaptopBattery && device.isPresent
    readonly property real pct: hasBattery ? device.percentage : 0
    readonly property bool charging: hasBattery && (device.state === UPowerDeviceState.Charging || device.state === UPowerDeviceState.FullyCharged)
    readonly property var profiles: [
        { id: PowerProfile.PowerSaver, label: "Power saver", icon: "leaf" },
        { id: PowerProfile.Balanced, label: "Balanced", icon: "gauge" },
        { id: PowerProfile.Performance, label: "Performance", icon: "zap", available: PowerProfiles.hasPerformanceProfile }
    ].filter(p => p.available !== false)
    readonly property var profile: profiles.find(p => p.id === PowerProfiles.profile) || profiles[1]

    shown: hasBattery
    icon: !hasBattery ? profile.icon
        : charging ? "battery-charging"
        : pct < 0.15 ? "battery-warning" : pct < 0.4 ? "battery-low" : pct < 0.75 ? "battery-medium" : "battery-full"
    text: hasBattery ? `${Math.round(pct * 100)}%` : ""
    urgent: hasBattery && !charging && pct < 0.1
    active: panel.visible
    tooltip: hasBattery ? status() : profile.label
    onClicked: panel.toggle()
    onRightClicked: {
        const i = profiles.findIndex(p => p.id === PowerProfiles.profile)
        PowerProfiles.profile = profiles[(i + 1) % profiles.length].id
    }

    function span(s) {
        const m = Math.round(s / 60), h = Math.floor(m / 60)
        return h > 0 ? `${h} h ${m % 60} min` : `${m} min`
    }
    function status() {
        if (!hasBattery) return ""
        switch (device.state) {
        case UPowerDeviceState.Charging: return device.timeToFull > 0 ? `Charging · full in ${span(device.timeToFull)}` : "Charging"
        case UPowerDeviceState.Discharging: return device.timeToEmpty > 0 ? `${span(device.timeToEmpty)} left` : "On battery"
        case UPowerDeviceState.FullyCharged: return "Fully charged"
        case UPowerDeviceState.PendingCharge: return "Plugged in, not charging"
        default: return UPowerDeviceState.toString(device.state)
        }
    }

    BarPanel {
        id: panel
        target: root
        title: root.hasBattery ? `Battery · ${Math.round(root.pct * 100)}%` : "Power"
        icon: root.hasBattery ? "battery-full" : "zap"
        contentWidth: 280

        ColumnLayout {
            visible: root.hasBattery
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 8
                radius: 4
                color: Theme.p.track
                Rectangle {
                    width: parent.width * root.pct
                    height: parent.height
                    radius: 4
                    color: root.urgent ? Theme.p.red : root.charging ? Theme.p.green : Theme.p.accent
                }
            }
            Label { role: "muted"; text: root.status() }
            Label {
                visible: root.hasBattery && root.device.healthSupported
                role: "caption"
                text: root.hasBattery ? `Health ${Math.round(root.device.healthPercentage)}% · ${root.device.energyRate.toFixed(1)} W` : ""
            }
        }

        Divider { visible: root.hasBattery }
        Label { role: "section"; text: "Power profile" }
        Repeater {
            model: root.profiles
            delegate: ListItem {
                required property var modelData
                icon: modelData.icon
                text: modelData.label
                highlighted: PowerProfiles.profile === modelData.id
                onClicked: PowerProfiles.profile = modelData.id
            }
        }
    }
}
