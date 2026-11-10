import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Services.Pipewire
import "../.."
import "../../services"
import "../../components"

// Left click: volume panel. Right click: mute. Scroll: volume.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "audio"
    readonly property var sink: Pipewire.defaultAudioSink
    readonly property var source: Pipewire.defaultAudioSource
    readonly property bool present: sink !== null && sink.audio !== null
    readonly property bool micPresent: source !== null && source.audio !== null
    readonly property real volume: present ? sink.audio.volume : 0
    readonly property bool muted: present && sink.audio.muted
    readonly property var sinks: Pipewire.nodes.values.filter(n => n.isSink && !n.isStream && n.audio)
    readonly property var sources: Pipewire.nodes.values.filter(n => !n.isSink && !n.isStream && n.audio)
    readonly property var streams: Pipewire.nodes.values.filter(n => n.isStream && n.isSink && n.audio)

    icon: muted ? "volume-x" : volume > 0.66 ? "volume-2" : volume > 0.2 ? "volume-1" : "volume"
    text: present ? `${Math.round(volume * 100)}%` : "—"
    active: panel.visible
    tooltip: present ? nodeName(sink) : "No audio output"
    onClicked: panel.toggle()
    onRightClicked: if (present) sink.audio.muted = !sink.audio.muted
    onScrolled: delta => { if (present) sink.audio.volume = Math.max(0, Math.min(1.5, volume + (delta > 0 ? 0.05 : -0.05))) }

    function nodeName(node) { return node ? (node.description || node.nickname || node.name) : "" }

    PwObjectTracker { objects: [root.sink, root.source].concat(root.streams).filter(n => n !== null) }

    BarPanel {
        id: panel
        target: root
        title: "Sound"
        icon: "volume-2"
        contentWidth: 320

        Label { role: "section"; text: "Output" }
        RowLayout {
            Layout.fillWidth: true
            IconButton {
                icon: root.muted ? "volume-x" : "volume-2"
                checked: root.muted
                tooltip: root.muted ? "Unmute" : "Mute"
                onClicked: if (root.present) root.sink.audio.muted = !root.muted
            }
            ThinSlider {
                id: outSlider
                Layout.fillWidth: true
                from: 0; to: 1.5
                onMoved: if (root.present) root.sink.audio.volume = value
            }
            Binding { target: outSlider; property: "value"; value: root.volume; when: !outSlider.pressed; restoreMode: Binding.RestoreNone }
            Label { text: `${Math.round(outSlider.value * 100)}%`; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignRight }
        }
        Repeater {
            model: root.sinks
            delegate: ListItem {
                required property var modelData
                icon: "volume-2"
                text: root.nodeName(modelData)
                highlighted: modelData === root.sink
                onClicked: Pipewire.preferredDefaultAudioSink = modelData
            }
        }

        Divider {}
        Label { role: "section"; text: "Input" }
        RowLayout {
            Layout.fillWidth: true
            visible: root.micPresent
            IconButton {
                icon: root.micPresent && root.source.audio.muted ? "mic-off" : "mic"
                checked: root.micPresent && root.source.audio.muted
                tooltip: "Mute microphone"
                onClicked: if (root.micPresent) root.source.audio.muted = !root.source.audio.muted
            }
            ThinSlider {
                id: inSlider
                Layout.fillWidth: true
                from: 0; to: 1
                onMoved: if (root.micPresent) root.source.audio.volume = value
            }
            Binding { target: inSlider; property: "value"; value: root.micPresent ? root.source.audio.volume : 0; when: !inSlider.pressed; restoreMode: Binding.RestoreNone }
            Label { text: `${Math.round(inSlider.value * 100)}%`; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignRight }
        }
        Repeater {
            model: root.sources
            delegate: ListItem {
                required property var modelData
                icon: "mic"
                text: root.nodeName(modelData)
                highlighted: modelData === root.source
                onClicked: Pipewire.preferredDefaultAudioSource = modelData
            }
        }

        Divider { visible: root.streams.length > 0 }
        Label { role: "section"; text: "Apps"; visible: root.streams.length > 0 }
        Repeater {
            model: root.streams
            delegate: RowLayout {
                id: streamRow
                required property var modelData
                Layout.fillWidth: true
                Label {
                    Layout.preferredWidth: 110
                    text: streamRow.modelData.properties["application.name"] || root.nodeName(streamRow.modelData)
                }
                ThinSlider {
                    id: streamSlider
                    Layout.fillWidth: true
                    from: 0; to: 1
                    onMoved: streamRow.modelData.audio.volume = value
                }
                Binding { target: streamSlider; property: "value"; value: streamRow.modelData.audio ? streamRow.modelData.audio.volume : 0; when: !streamSlider.pressed; restoreMode: Binding.RestoreNone }
            }
        }
    }
}
