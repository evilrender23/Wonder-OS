import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// CPU load; the panel adds history, per-thread bars, temperature and the
// hungriest processes (killable).
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "cpu"
    icon: "cpu"
    text: `${SysStats.cpu}%`
    urgent: SysStats.cpu >= 90
    active: panel.visible
    tooltip: `Load ${SysStats.load}`
    onClicked: panel.toggle()
    onRightClicked: Session.terminal("btop || top")

    BarPanel {
        id: panel
        target: root
        title: `CPU · ${SysStats.cpu}%`
        icon: "cpu"
        contentWidth: 320
        onVisibleChanged: { SysStats.sortProcessesBy = "cpu"; SysStats.wantProcesses = visible }

        Sparkline { Layout.fillWidth: true; values: SysStats.history }

        RowLayout {
            Layout.fillWidth: true
            Label { role: "muted"; text: `Load ${SysStats.load}`; Layout.fillWidth: true }
            Label { visible: SysStats.temperature > 0; role: "muted"; text: `${SysStats.temperature} °C` }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 3
            Repeater {
                model: SysStats.threads
                delegate: Rectangle {
                    required property int modelData
                    width: 12; height: 22; radius: 2
                    color: Theme.p.track
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * parent.modelData / 100
                        radius: 2
                        color: parent.modelData > 85 ? Theme.p.red : Theme.p.accent
                    }
                }
            }
        }

        Divider {}
        Label { role: "section"; text: "Top processes" }
        Repeater {
            model: SysStats.topProcesses
            delegate: ListItem {
                required property var modelData
                text: modelData.name
                trailing: `${modelData.cpu.toFixed(1)}%  ·  ${modelData.mem.toFixed(0)} MB`
                actionIcon: "x"
                actionTooltip: `Kill ${modelData.pid}`
                clickable: false
                onActionClicked: SysStats.kill(modelData.pid)
            }
        }
    }
}
