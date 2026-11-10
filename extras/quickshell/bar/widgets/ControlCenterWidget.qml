import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Bluetooth
import Quickshell.Services.Pipewire
import Quickshell.Services.UPower
import "../.."
import "../../services"
import "../../components"

// macOS Control Centre: Wi-Fi, Bluetooth, Focus (do not disturb), keep
// awake, display brightness, sound, now playing, appearance and displays in
// one panel. Clicking a module's name opens that widget's full panel.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "control"
    icon: "control-center"
    active: panel.visible
    tooltip: I18n.tr("Centro de control")
    onClicked: panel.toggle()

    readonly property var adapter: Bluetooth.defaultAdapter
    readonly property bool btOn: adapter !== null && adapter.enabled
    readonly property var btConnected: adapter ? adapter.devices.values.filter(d => d.connected) : []
    readonly property var sink: Pipewire.defaultAudioSink
    readonly property bool hasSink: sink !== null && sink.audio !== null

    PwObjectTracker { objects: root.sink ? [root.sink] : [] }

    // Opens another widget's panel (on the first screen) after this one closes.
    function openPanel(id) { panel.close(); Popups.panelRequested(id) }

    BarPanel {
        id: panel
        target: root
        contentWidth: 320
        padding: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ControlTile {
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                ControlToggle {
                    icon: Net.wired ? "network" : Net.icon
                    text: Net.wired ? I18n.tr("Red") : "Wi-Fi"
                    detail: Net.wired ? I18n.tr("Cable") : Net.ssid ? Net.ssid : Net.radio ? I18n.tr("Sin conexión") : I18n.tr("No")
                    checked: Net.wired || (Net.radio && Net.ssid !== "")
                    openable: true
                    onToggled: if (!Net.wired) Net.setRadio(!Net.radio)
                    onOpened: root.openPanel("network")
                }
                ControlToggle {
                    visible: root.adapter !== null
                    icon: root.btOn ? "bluetooth" : "bluetooth-off"
                    text: "Bluetooth"
                    detail: !root.btOn ? I18n.tr("No") : root.btConnected.length ? root.btConnected.map(d => d.name).join(", ") : I18n.tr("Sí")
                    checked: root.btOn
                    openable: true
                    onToggled: root.adapter.enabled = !root.adapter.enabled
                    onOpened: root.openPanel("bluetooth")
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 1
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 8
                ControlTile {
                    ControlToggle {
                        icon: "moon"
                        text: I18n.tr("Concentración")
                        detail: Notifs.dnd ? I18n.tr("No molestar") : I18n.tr("Desactivada")
                        checked: Notifs.dnd
                        onToggled: Settings.data.doNotDisturb = !Settings.data.doNotDisturb
                    }
                }
                ControlTile {
                    ControlToggle {
                        icon: "coffee"
                        text: I18n.tr("Mantener activo")
                        detail: Session.idleInhibited ? I18n.tr("Sí") : I18n.tr("No")
                        checked: Session.idleInhibited
                        onToggled: Session.idleInhibited = !Session.idleInhibited
                    }
                }
            }
        }

        ControlTile {
            visible: Brightness.present
            Text { text: I18n.tr("Pantalla"); color: Theme.p.text; font.pixelSize: Theme.fontSize - 1; font.family: Theme.fontFamily; font.weight: Font.DemiBold }
            RowLayout {
                Layout.fillWidth: true
                Icon { name: "sun-dim"; size: 14 }
                ThinSlider {
                    id: bright
                    Layout.fillWidth: true
                    from: 1; to: 100; stepSize: 1
                    onMoved: Brightness.set(value)
                }
                Binding { target: bright; property: "value"; value: Brightness.fraction * 100; when: !bright.pressed; restoreMode: Binding.RestoreNone }
                Icon { name: "sun"; size: 14 }
            }
        }

        ControlTile {
            visible: root.hasSink
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: I18n.tr("Sonido"); color: Theme.p.text; font.pixelSize: Theme.fontSize - 1; font.family: Theme.fontFamily; font.weight: Font.DemiBold }
                IconButton { icon: "chevron-right"; size: 20; tooltip: I18n.tr("Salidas y aplicaciones"); onClicked: root.openPanel("audio") }
            }
            RowLayout {
                Layout.fillWidth: true
                IconButton {
                    icon: root.hasSink && root.sink.audio.muted ? "volume-x" : "volume-1"
                    size: 22
                    onClicked: if (root.hasSink) root.sink.audio.muted = !root.sink.audio.muted
                }
                ThinSlider {
                    id: vol
                    Layout.fillWidth: true
                    from: 0; to: 1
                    onMoved: if (root.hasSink) root.sink.audio.volume = value
                }
                Binding { target: vol; property: "value"; value: root.hasSink ? root.sink.audio.volume : 0; when: !vol.pressed; restoreMode: Binding.RestoreNone }
                Icon { name: "volume-2"; size: 14 }
            }
        }

        ControlTile {
            visible: Media.present
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    implicitWidth: 40; implicitHeight: 40
                    radius: 8
                    color: Theme.p.track
                    clip: true
                    Image { anchors.fill: parent; source: Media.art; fillMode: Image.PreserveAspectCrop; visible: Media.art !== "" }
                    Icon { anchors.centerIn: parent; visible: Media.art === ""; name: "music"; size: 18 }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label { Layout.fillWidth: true; text: Media.title; font.weight: Font.DemiBold }
                    Label { Layout.fillWidth: true; role: "muted"; text: Media.artist || (Media.present ? Media.player.identity : "") }
                }
                IconButton { icon: "skip-back"; size: 26; enabled: Media.present && Media.player.canGoPrevious; onClicked: Media.player.previous() }
                IconButton { icon: Media.playing ? "pause" : "play"; size: 26; onClicked: Media.player.togglePlaying() }
                IconButton { icon: "skip-forward"; size: 26; enabled: Media.present && Media.player.canGoNext; onClicked: Media.player.next() }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ControlTile {
                Layout.preferredWidth: 1
                ControlToggle {
                    icon: Theme.dark ? "moon-star" : "sun"
                    text: I18n.tr("Modo oscuro")
                    detail: Theme.dark ? I18n.tr("Sí") : I18n.tr("No")
                    checked: Theme.dark
                    onToggled: Settings.data.themeMode = Theme.dark ? "light" : "dark"
                }
            }
            ControlTile {
                Layout.preferredWidth: 1
                ControlToggle {
                    icon: "monitor"
                    text: I18n.tr("Pantallas")
                    detail: (Quickshell.screens.length === 1 ? I18n.tr("%1 conectada") : I18n.tr("%1 conectadas")).arg(String(Quickshell.screens.length))
                    openable: true
                    onToggled: { panel.close(); Popups.displaysRequested() }
                    onOpened: { panel.close(); Popups.displaysRequested() }
                }
            }
        }
    }
}
