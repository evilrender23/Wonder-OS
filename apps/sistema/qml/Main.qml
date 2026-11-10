import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Schema.js" as Schema

ApplicationWindow {
    id: window

    property int current: Math.max(0, Schema.pages.findIndex(p => p.id === startPage))
    /// What the sidebar's search holds; while there is something, its results take the place of the page.
    readonly property string query: search.text.trim()
    /// Where a search result leads: the group to bring into view and the text to look the shortcuts up by.
    property int pendingGroup: -1
    property string pendingQuery: ""

    function open(page, group, shortcuts) {
        pendingGroup = group
        pendingQuery = shortcuts
        current = page
        search.clear()
    }

    width: 1020
    height: 700
    minimumWidth: 820
    minimumHeight: 480
    visible: true
    title: qsTr("Sistema")
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
        sequences: [StandardKey.Find]
        onActivated: {
            search.forceActiveFocus()
            search.selectAll()
        }
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
                    text: qsTr("Sistema")
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                TField {
                    id: search

                    Layout.fillWidth: true
                    Layout.bottomMargin: 8
                    rightPadding: 28
                    placeholderText: qsTr("Buscar")
                    text: startSearch
                    Keys.onEscapePressed: clear()

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 9
                        anchors.verticalCenter: parent.verticalCenter
                        visible: search.text !== ""
                        text: "✕"
                        color: clearHover.hovered ? Theme.text : Theme.dim
                        font.pixelSize: Theme.small

                        HoverHandler {
                            id: clearHover
                            cursorShape: Qt.ArrowCursor
                        }
                        TapHandler {
                            onTapped: search.clear()
                        }
                    }
                }

                Repeater {
                    model: Schema.pages

                    Rectangle {
                        id: entry

                        required property var modelData
                        required property int index

                        /// No page is the current one while the results of a search are up.
                        readonly property bool selected: window.current === index && window.query === ""

                        Layout.fillWidth: true
                        implicitHeight: 34
                        radius: 8
                        color: selected ? Theme.accent : (entryHover.hovered ? Theme.hover : "transparent")

                        HoverHandler {
                            id: entryHover
                        }
                        TapHandler {
                            onTapped: window.open(entry.index, -1, "")
                        }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 10
                            spacing: 9

                            Text {
                                width: 18
                                horizontalAlignment: Text.AlignHCenter
                                text: entry.modelData.icon
                                color: entry.selected ? "#ffffff" : Theme.dim
                                font.pixelSize: Theme.body
                            }
                            Text {
                                text: entry.modelData.title
                                color: entry.selected ? "#ffffff" : Theme.text
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
                    text: qsTr("Deshacer el último cambio")
                    enabled: Config.canUndo
                    onClicked: Config.undo()
                }
                TButton {
                    Layout.fillWidth: true
                    text: qsTr("Abrir config.toml")
                    tip: Config.path + "\n" + qsTr("Las reglas de ventanas y los monitores se editan ahí.")
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
                            : qsTr("No se encuentra driftwm: los cambios se guardan sin que el escritorio los compruebe.")
                        color: Theme.danger
                        font.pixelSize: Theme.small
                        wrapMode: Text.Wrap
                    }
                    TButton {
                        visible: Config.error !== ""
                        text: qsTr("Recargar")
                        onClicked: Config.reload()
                    }
                    TButton {
                        visible: Config.error !== ""
                        text: qsTr("Cerrar")
                        onClicked: Config.clearError()
                    }
                }
            }

            Loader {
                id: pageLoader

                readonly property var page: Schema.pages[window.current]

                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: window.query !== "" ? searchPage : (page.shortcuts ? shortcutsPage : (page.folders ? foldersPage : settingsPage))
            }
        }
    }

    Component {
        id: searchPage

        SearchPage {
            query: window.query
            onOpened: (page, group, shortcuts) => window.open(page, group, shortcuts)
        }
    }
    Component {
        id: shortcutsPage

        ShortcutsPage {
            startQuery: window.pendingQuery
        }
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
            startGroup: window.pendingGroup
        }
    }
}
