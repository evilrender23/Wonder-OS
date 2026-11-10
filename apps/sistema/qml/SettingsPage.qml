import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/// A page of Schema.js: its groups, each a card of settings and/or a table of bindings.
ScrollView {
    id: root

    required property var page

    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 0

        Text {
            Layout.leftMargin: 28
            Layout.topMargin: 22
            Layout.bottomMargin: 4
            text: root.page.title
            color: Theme.text
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }

        Repeater {
            model: root.page.groups

            ColumnLayout {
                id: group

                required property var modelData

                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 16
                spacing: 6

                Text {
                    Layout.leftMargin: 4
                    text: group.modelData.title
                    color: Theme.text
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.leftMargin: 4
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: group.modelData.note || ""
                    color: Theme.dim
                    font.pixelSize: Theme.small
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: card.implicitHeight > 0
                    implicitHeight: card.implicitHeight
                    radius: Theme.radius
                    color: Theme.card
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: card

                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 0

                        Repeater {
                            model: group.modelData.items || []

                            ColumnLayout {
                                id: entry

                                required property var modelData
                                required property int index

                                Layout.fillWidth: true
                                spacing: 0

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 14
                                    visible: entry.index > 0
                                    implicitHeight: 1
                                    color: Theme.border
                                }
                                SettingRow {
                                    Layout.fillWidth: true
                                    setting: entry.modelData
                                }
                            }
                        }
                        Loader {
                            Layout.fillWidth: true
                            active: group.modelData.bindings !== undefined
                            visible: active
                            sourceComponent: BindingsTable {
                                spec: group.modelData.bindings
                            }
                        }
                    }
                }
            }
        }

        Item {
            implicitHeight: 28
        }
    }
}
