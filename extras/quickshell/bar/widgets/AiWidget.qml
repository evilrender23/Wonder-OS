import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// Quota use of AI coding tools (Claude Code, Codex, OpenCode) from the
// extras/scripts/*-usage caches. Shows the highest window; the panel has
// every provider and window with its reset time.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "ai"
    shown: AiUsage.providers.length > 0
    icon: "bot"
    text: `${AiUsage.maxPercent}%`
    urgent: AiUsage.maxPercent >= 90
    iconColor: AiUsage.maxPercent >= 75 ? Theme.p.orange : Theme.p.text2
    active: panel.visible
    tooltip: AiUsage.providers.map(p => `${p.name} ${p.windows.map(w => w.percent + "%").join(" / ")}`).join(" · ")
    onClicked: panel.toggle()
    onRightClicked: AiUsage.refreshNow()

    BarPanel {
        id: panel
        target: root
        title: "AI usage"
        icon: "bot"
        contentWidth: 320

        headerExtra: IconButton { icon: "refresh-cw"; size: 24; tooltip: "Refresh"; onClicked: AiUsage.refreshNow() }

        Repeater {
            model: AiUsage.providers
            delegate: ColumnLayout {
                id: prov
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: 6

                Divider { visible: prov.index > 0 }
                RowLayout {
                    Layout.fillWidth: true
                    Label { role: "title"; text: prov.modelData.name; Layout.fillWidth: true }
                    Label { role: "caption"; text: [prov.modelData.plan, prov.modelData.stale ? "stale" : ""].filter(Boolean).join(" · ") }
                }
                Repeater {
                    model: prov.modelData.windows
                    delegate: ColumnLayout {
                        id: win
                        required property var modelData
                        readonly property color tone: modelData.percent >= 90 ? Theme.p.red : modelData.percent >= 75 ? Theme.p.orange : Theme.p.accent
                        Layout.fillWidth: true
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: win.modelData.label; Layout.fillWidth: true }
                            Label { role: "caption"; text: win.modelData.resetsIn ? `resets in ${win.modelData.resetsIn}` : "" }
                            Label { text: `${win.modelData.percent}%`; color: win.tone; font.weight: Font.DemiBold }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 6
                            radius: 3
                            color: Theme.p.track
                            Rectangle {
                                width: parent.width * Math.min(100, win.modelData.percent) / 100
                                height: parent.height
                                radius: 3
                                color: win.tone
                                Behavior on width { NumberAnimation { duration: Theme.animMs * 2; easing.type: Easing.OutCubic } }
                            }
                        }
                    }
                }
                Label {
                    visible: prov.modelData.today > 0
                    role: "caption"
                    text: `Today ${Math.round(prov.modelData.today / 1000)}k tokens` + (prov.modelData.perHour > 0 ? ` · ${Math.round(prov.modelData.perHour / 1000)}k/h` : "")
                }
            }
        }
        Label { visible: AiUsage.providers.length === 0; role: "muted"; text: "No usage caches. Run extras/scripts/ai-usage/install.sh." }
    }
}
