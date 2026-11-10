import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// Left click: Wi-Fi panel. Right click: NetworkManager's editor.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "network"
    icon: Net.icon
    text: Net.wired ? "" : Net.ssid ? `${Net.signal}%` : ""
    active: panel.visible
    tooltip: Net.summary
    onClicked: panel.toggle()
    onRightClicked: Quickshell.execDetached(["sh", "-c", "nm-connection-editor || ${TERMINAL:-wonderterm} -e nmtui"])

    BarPanel {
        id: panel
        target: root
        title: "Network"
        icon: Net.icon
        contentWidth: 320
        onVisibleChanged: { Net.scanning = visible; pending.network = null; password.text = "" }

        headerExtra: IconButton { icon: "refresh-cw"; size: 24; tooltip: "Scan"; onClicked: Net.refresh(true) }

        ListItem {
            visible: Net.wired
            icon: "network"
            text: "Wired"
            detail: Net.wiredDevice
            highlighted: true
            clickable: false
        }

        Switch {
            visible: Net.hasWifi
            text: "Wi-Fi"
            detail: Net.ssid ? `Connected to ${Net.ssid}` : Net.radio ? "Not connected" : "Off"
            checked: Net.radio
            onToggled: v => Net.setRadio(v)
        }

        Flickable {
            visible: Net.radio && Net.hasWifi
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
                    model: Net.networks
                    delegate: ListItem {
                        required property var modelData
                        icon: modelData.signal > 66 ? "wifi" : modelData.signal > 33 ? "wifi-high" : "wifi-low"
                        text: modelData.ssid
                        detail: (modelData.active ? "Connected · " : modelData.known ? "Saved · " : "")
                            + (modelData.security ? modelData.security.replace(/\s+/g, " ") : "Open")
                        trailing: `${modelData.signal}%`
                        highlighted: modelData.active
                        actionIcon: modelData.known ? "trash-2" : ""
                        actionTooltip: "Forget"
                        onClicked: {
                            if (Net.needsPassword(modelData)) { pending.network = modelData; password.focusInput() }
                            else Net.connect(modelData)
                        }
                        onActionClicked: Net.forget(modelData)
                    }
                }
            }
        }

        QtObject { id: pending; property var network: null }

        RowLayout {
            visible: pending.network !== null
            Layout.fillWidth: true
            TextBox {
                id: password
                Layout.fillWidth: true
                password: true
                placeholder: pending.network ? `Password for ${pending.network.ssid}` : ""
                onAccepted: join.clicked()
            }
            IconButton {
                id: join
                text: "Join"
                primary: true
                onClicked: { Net.connect(pending.network, password.text); pending.network = null; password.text = "" }
            }
        }

        Label { visible: Net.busy !== ""; role: "muted"; text: `${Net.busy}…` }
        Label { visible: Net.error !== ""; role: "caption"; color: Theme.p.red; text: Net.error; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label { visible: !Net.available; role: "muted"; text: "NetworkManager isn't running." }
    }
}
