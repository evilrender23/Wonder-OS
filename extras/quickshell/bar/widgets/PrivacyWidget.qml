import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Services.Pipewire
import "../.."
import "../../services"
import "../../components"

// macOS privacy indicators: an orange dot while something records the
// microphone, a green one while something captures the camera (PipeWire
// capture streams). Hidden when nothing does.
Item {
    id: root

    property var bar
    property string widgetId: "privacy"
    readonly property var captures: Pipewire.nodes.values.filter(n => n.isStream && !n.isSink)
    readonly property var mic: captures.filter(n => n.audio !== null)
    readonly property var camera: captures.filter(n => n.audio === null)
    readonly property bool shown: mic.length > 0 || camera.length > 0

    implicitWidth: shown ? dots.implicitWidth + 10 : 0
    implicitHeight: Theme.pillHeight

    function names(nodes) {
        const seen = []
        for (const n of nodes) {
            const p = n.properties || {}
            const name = p["application.name"] || n.description || n.name
            if (name && !seen.includes(name)) seen.push(name)
        }
        return seen.join(", ")
    }

    RowLayout {
        id: dots
        anchors.centerIn: parent
        spacing: 4
        Rectangle { visible: root.mic.length > 0; implicitWidth: 7; implicitHeight: 7; radius: 3.5; color: "#ff9f0a" }
        Rectangle { visible: root.camera.length > 0; implicitWidth: 7; implicitHeight: 7; radius: 3.5; color: "#30d158" }
    }

    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true }

    Tooltip {
        target: root
        shown: mouse.containsMouse && root.shown
        text: [root.mic.length ? `Micrófono: ${root.names(root.mic)}` : "",
               root.camera.length ? `Cámara: ${root.names(root.camera)}` : ""].filter(s => s).join("\n")
    }
}
