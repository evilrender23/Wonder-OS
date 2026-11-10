import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Actions.js" as Actions
import "Bindings.js" as Bindings
import "Schema.js" as Schema
import "Search.js" as Search

/// What the sidebar's search finds: the settings themselves, ready to change, under the page and
/// group they belong to, and links to the pages, tables and keyboard shortcuts that match.
ScrollView {
    id: root

    required property string query

    /// Go to a page; `group` is the group to bring into view (-1: none) and `shortcuts` the
    /// text to search the keyboard shortcuts for.
    signal opened(int page, int group, string shortcuts)

    readonly property var found: Search.find(Schema.pages, query)
    readonly property int shortcutsPage: Schema.pages.findIndex(p => p.shortcuts === true)
    readonly property int shortcutCount: {
        Config.revision
        const ws = Search.words(query)
        if (ws.length === 0)
            return 0
        const table = ["keybindings"]
        const modKey = String(Config.has(["mod_key"]) ? Config.get(["mod_key"]) : App.fallback(["mod_key"]))
        const norm = key => Bindings.normalize(key, modKey, name => App.canonicalKey(name))
        const off = Config.has(["bindings", "disable_defaults"]) ? Config.get(["bindings", "disable_defaults"]) : []
        return Bindings.rows(App.fallbackEntries(table), Config.entries(table), norm, off.indexOf("keys") >= 0)
            .filter(r => Search.has(Search.fold([Actions.describe(r.value), r.value, r.key, Bindings.caps(r.key, modKey).join(" ")].join(" ")), ws))
            .length
    }
    readonly property var links: {
        const out = found.pages.filter(p => p.index !== shortcutsPage || shortcutCount === 0)
            .map(p => ({ page: p.index, group: -1, shortcuts: "", icon: p.icon, title: p.title, detail: qsTr("Apartado") }))
        if (shortcutCount > 0)
            out.push({
                page: shortcutsPage, group: -1, shortcuts: query.trim(), icon: Schema.pages[shortcutsPage].icon,
                title: Schema.pages[shortcutsPage].title,
                detail: shortcutCount === 1 ? qsTr("1 atajo coincide") : qsTr("%1 atajos coinciden").arg(shortcutCount)
            })
        for (const g of found.groups)
            if (g.table)
                out.push({ page: g.page, group: g.group, shortcuts: "", icon: Schema.pages[g.page].icon, title: g.pageTitle + " › " + g.title, detail: qsTr("Tabla") })
        return out
    }
    readonly property var groups: found.groups.filter(g => g.items.length > 0)

    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 0

        Text {
            Layout.leftMargin: 28
            Layout.rightMargin: 24
            Layout.topMargin: 22
            Layout.bottomMargin: 4
            Layout.fillWidth: true
            text: qsTr("Resultados de «%1»").arg(root.query.trim())
            color: Theme.text
            font.pixelSize: 22
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            Layout.leftMargin: 28
            Layout.rightMargin: 24
            Layout.topMargin: 10
            Layout.fillWidth: true
            visible: root.links.length === 0 && root.groups.length === 0
            text: qsTr("Ningún ajuste coincide. Prueba con otra palabra, o con la clave de config.toml.")
            color: Theme.dim
            font.pixelSize: Theme.body
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 16
            visible: root.links.length > 0
            implicitHeight: linkRows.implicitHeight
            radius: Theme.radius
            color: Theme.card
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: linkRows

                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 0

                Repeater {
                    model: root.links

                    Item {
                        id: link

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        implicitHeight: 42

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 1
                            radius: Theme.radius - 1
                            color: linkHover.hovered ? Theme.hover : "transparent"
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 14
                            visible: link.index > 0
                            height: 1
                            color: Theme.border
                        }
                        HoverHandler {
                            id: linkHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: root.opened(link.modelData.page, link.modelData.group, link.modelData.shortcuts)
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14
                            spacing: 9

                            Text {
                                Layout.preferredWidth: 18
                                horizontalAlignment: Text.AlignHCenter
                                text: link.modelData.icon
                                color: Theme.dim
                                font.pixelSize: Theme.body
                            }
                            Text {
                                Layout.fillWidth: true
                                text: link.modelData.title
                                color: Theme.text
                                font.pixelSize: Theme.body
                                elide: Text.ElideRight
                            }
                            Text {
                                text: link.modelData.detail
                                color: Theme.dim
                                font.pixelSize: Theme.small
                            }
                            Text {
                                text: "›"
                                color: Theme.faint
                                font.pixelSize: 18
                            }
                        }
                    }
                }
            }
        }

        Repeater {
            model: root.groups

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
                    text: group.modelData.pageTitle + " › " + group.modelData.title
                    color: whereHover.hovered ? Theme.accent : Theme.text
                    font.pixelSize: Theme.body
                    font.weight: Font.DemiBold

                    HoverHandler {
                        id: whereHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: root.opened(group.modelData.page, group.modelData.group, "")
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
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
                            model: group.modelData.items

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
                    }
                }
            }
        }

        Item {
            implicitHeight: 28
        }
    }
}
