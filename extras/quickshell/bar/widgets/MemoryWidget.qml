import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "memory"
    icon: "memory-stick"
    text: `${SysStats.memPercent}%`
    urgent: SysStats.memPercent >= 90
    active: panel.visible
    tooltip: `${SysStats.memUsed.toFixed(1)} / ${SysStats.memTotal.toFixed(1)} GiB`
    onClicked: panel.toggle()

    component Meter: ColumnLayout {
        id: meter
        property string label
        property real used
        property real total
        readonly property real frac: total > 0 ? used / total : 0
        Layout.fillWidth: true
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Label { text: meter.label; Layout.fillWidth: true }
            Label { role: "muted"; text: `${meter.used.toFixed(1)} / ${meter.total.toFixed(1)} GiB` }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 8
            radius: 4
            color: Theme.p.track
            Rectangle {
                width: parent.width * meter.frac
                height: parent.height
                radius: 4
                color: meter.frac > 0.9 ? Theme.p.red : Theme.p.accent
            }
        }
    }

    BarPanel {
        id: panel
        target: root
        title: `Memory · ${SysStats.memPercent}%`
        icon: "memory-stick"
        contentWidth: 320
        onVisibleChanged: { SysStats.sortProcessesBy = "mem"; SysStats.wantProcesses = visible }

        Meter { label: "RAM"; used: SysStats.memUsed; total: SysStats.memTotal }
        Meter { visible: SysStats.swapTotal > 0; label: "Swap"; used: SysStats.swapUsed; total: SysStats.swapTotal }

        Divider {}
        Label { role: "section"; text: "Top processes" }
        Repeater {
            model: SysStats.topProcesses
            delegate: ListItem {
                required property var modelData
                text: modelData.name
                trailing: `${modelData.mem.toFixed(0)} MB`
                actionIcon: "x"
                actionTooltip: `Kill ${modelData.pid}`
                clickable: false
                onActionClicked: SysStats.kill(modelData.pid)
            }
        }
    }
}
