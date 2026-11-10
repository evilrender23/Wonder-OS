import QtQuick
import QtQuick.Layouts
import Quickshell
import "../.."
import "../../services"
import "../../components"

// Pending updates (see services/Updates.qml). Hidden when there are none
// (unless checking).
// Left click: list; right click: check now.
Pill {
    id: root

    property var bar
    panel: panel
    widgetId: "updates"
    shown: Updates.count > 0 || Updates.checking || panel.visible
    icon: Updates.checking ? "refresh-cw" : "package"
    text: Updates.count > 0 ? `${Updates.count}` : ""
    iconColor: Updates.count > 0 ? Theme.p.accent : Theme.p.text3
    active: panel.visible
    tooltip: Updates.checking ? "Checking for updates…" : Updates.sections.map(s => `${s.items.length} ${s.title}`).join(" · ")
    onClicked: panel.toggle()
    onRightClicked: Updates.check()

    BarPanel {
        id: panel
        target: root
        title: Updates.count > 0 ? `${Updates.count} updates` : "Up to date"
        icon: Updates.count > 0 ? "package" : "package-check"
        contentWidth: 340

        headerExtra: IconButton { icon: "refresh-cw"; size: 24; tooltip: "Check now"; checked: Updates.checking; onClicked: Updates.check() }

        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 300)
            contentHeight: list.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: list
                width: parent.width
                spacing: 0
                Repeater {
                    model: Updates.sections
                    delegate: ColumnLayout {
                        id: section
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.topMargin: index > 0 ? 8 : 0
                        spacing: 0
                        Label { role: "section"; text: section.modelData.title }
                        Repeater {
                            model: section.modelData.items
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Label { text: modelData.name; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { role: "caption"; text: modelData.from ? `${modelData.from} → ${modelData.to}` : modelData.to }
                            }
                        }
                    }
                }
                Repeater {
                    model: Updates.commits.slice(0, 8)
                    delegate: Label {
                        required property string modelData
                        Layout.fillWidth: true
                        role: "caption"
                        text: `· ${modelData}`
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Label { visible: Updates.count === 0 && !Updates.checking; role: "muted"; text: "Nothing to update." }
        RowLayout {
            Layout.fillWidth: true
            Label { role: "caption"; text: Updates.lastChecked ? `Checked ${Updates.lastChecked}` : ""; Layout.fillWidth: true }
            IconButton {
                visible: Updates.count > 0
                icon: "terminal"
                text: Updates.wonderUpdate ? "Update with WonderUpdate" : Updates.helper ? `Update with ${Updates.helper}` : "Update"
                primary: true
                onClicked: { Updates.apply(); panel.close() }
            }
        }
        Label { visible: Updates.error !== ""; role: "caption"; color: Theme.p.red; text: Updates.error }
    }
}
