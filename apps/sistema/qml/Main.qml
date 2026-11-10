import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Schema.js" as Schema

ApplicationWindow {
    id: window

    property int current: Math.max(0, Schema.pages.findIndex(p => p.id === startPage))

    width: 1020
    height: 700
    minimumWidth: 820
    minimumHeight: 480
    visible: true
    title: "Sistema"
    color: Theme.window

    // The stock controls (switches, combo boxes, check boxes) take the desktop's colours from here.
    palette.window: Theme.card
    palette.windowText: Theme.text
    palette.base: Theme.card
    palette.text: Theme.text
    palette.button: Theme.dark ? "#3a3a3c" : "#e9e9ec"
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: "#ffffff"
    palette.mid: Theme.border
    palette.dark: Theme.dim
    palette.placeholderText: Theme.faint

    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: Config.canUndo
        onActivated: Config.undo()
    }
    Shortcut {
        sequence: StandardKey.Quit
        onActivated: Qt.quit()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            implicitWidth: 230
            color: Theme.sidebar

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 2

                Text {
                    Layout.leftMargin: 8
                    Layout.topMargin: 10
                    Layout.bottomMargin: 10
                    text: "Sistema"
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: Schema.pages

                    Rectangle {
                        id: entry

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        implicitHeight: 34
                        radius: 8
                        color: window.current === index ? Theme.accent : (entryHover.hovered ? Theme.hover : "transparent")

                        HoverHandler {
                            id: entryHover
                        }
                        TapHandler {
                            onTapped: window.current = entry.index
                        }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 10
                            spacing: 9

                            Text {
                                width: 18
                                horizontalAlignment: Text.AlignHCenter
                                text: entry.modelData.icon
                                color: window.current === entry.index ? "#ffffff" : Theme.dim
                                font.pixelSize: Theme.body
                            }
                            Text {
                                text: entry.modelData.title
                                color: window.current === entry.index ? "#ffffff" : Theme.text
                                font.pixelSize: Theme.body
                            }
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
                TButton {
                    Layout.fillWidth: true
                    text: "Deshacer el último cambio"
                    enabled: Config.canUndo
                    onClicked: Config.undo()
                }
                TButton {
                    Layout.fillWidth: true
                    text: "Abrir config.toml"
                    tip: Config.path + "\nLas reglas de ventanas y los monitores se editan ahí."
                    onClicked: App.openFile(Config.path)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                visible: Config.error !== "" || !Config.checked
                implicitHeight: banner.implicitHeight + 20
                color: Theme.dangerFill

                RowLayout {
                    id: banner

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 28
                    anchors.rightMargin: 16
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: Config.error !== "" ? Config.error
                            : "No se encuentra driftwm: los cambios se guardan sin que el escritorio los compruebe."
                        color: Theme.danger
                        font.pixelSize: Theme.small
                        wrapMode: Text.Wrap
                    }
                    TButton {
                        visible: Config.error !== ""
                        text: "Recargar"
                        onClicked: Config.reload()
                    }
                    TButton {
                        visible: Config.error !== ""
                        text: "Cerrar"
                        onClicked: Config.clearError()
                    }
                }
            }

            Loader {
                id: pageLoader

                readonly property var page: Schema.pages[window.current]

                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: page.shortcuts ? shortcutsPage : (page.folders ? foldersPage : settingsPage)
            }
        }
    }

    Component {
        id: shortcutsPage

        ShortcutsPage {}
    }
    Component {
        id: foldersPage

        FoldersPage {
            page: pageLoader.page
        }
    }
    Component {
        id: settingsPage

        SettingsPage {
            page: pageLoader.page
        }
    }
}
