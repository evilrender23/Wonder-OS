import QtQuick
import QtQuick.Controls.Basic

/// The one bar: the document on the left, the tools in the middle, and the
/// palette, sharing and the inspector on the right.
Item {
    id: bar
    required property var host
    readonly property var book: host.book

    Connections {
        target: bar.host
        function onShareMenuWanted() {
            shareMenu.popup(share, 0, share.height + 4)
        }
    }

    Row {
        id: left
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        TButton {
            anchors.verticalCenter: parent.verticalCenter
            symbol: "panel-left"
            checked: bar.host.showSidebar
            onClicked: bar.host.run("sidebar")
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: titleRow.implicitWidth + 16
            height: 28
            radius: 7
            color: titleArea.containsMouse ? Theme.hover : "transparent"
            Row {
                id: titleRow
                anchors.centerIn: parent
                spacing: 7
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, Math.max(70, tools.x - left.x - 90))
                    text: bar.book.title
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                // Changes not saved yet.
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: bar.book.modified
                    width: 7
                    height: 7
                    radius: 3.5
                    color: Theme.tint
                }
            }
            MouseArea {
                id: titleArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: bar.host.run("rename")
            }
        }
    }

    component Divider: Item {
        width: 13
        height: 26
        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: 16
            color: Theme.border
        }
    }

    Row {
        id: tools
        readonly property real room: right.x - (left.x + left.width)
        x: Math.max(left.x + left.width + 8, Math.min((bar.width - width) / 2, right.x - width - 8))
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        TButton { symbol: "undo-2"; active: bar.book.canUndo; onClicked: bar.host.run("undo") }
        TButton { symbol: "redo-2"; active: bar.book.canRedo; onClicked: bar.host.run("redo") }
        Divider {}
        TButton { symbol: "bold"; checked: bar.host.info.bold === true; onClicked: bar.host.run("bold") }
        TButton { symbol: "italic"; checked: bar.host.info.italic === true; onClicked: bar.host.run("italic") }
        Divider { visible: alignLeft.visible }
        TButton { id: alignLeft; visible: bar.width >= 1000; symbol: "align-left"; checked: bar.host.info.align === 1; onClicked: bar.host.run("align-left") }
        TButton { visible: alignLeft.visible; symbol: "align-center"; checked: bar.host.info.align === 2; onClicked: bar.host.run("align-center") }
        TButton { visible: alignLeft.visible; symbol: "align-right"; checked: bar.host.info.align === 3; onClicked: bar.host.run("align-right") }
        Divider {}
        TButton { symbol: "euro"; checked: bar.host.info.family === "currency"; onClicked: bar.host.run("format-currency") }
        TButton { symbol: "percent"; checked: bar.host.info.family === "percent"; onClicked: bar.host.run("format-percent") }
        Divider {}
        TButton {
            id: sigma
            symbol: "sigma"
            onClicked: sumMenu.popup(sigma, 0, sigma.height + 4)
        }
        TButton {
            id: charts
            symbol: "chart-column"
            onClicked: chartMenu.popup(charts, 0, charts.height + 4)
        }
        TButton { symbol: "arrow-down-a-z"; onClicked: bar.host.run("sort-asc") }
        TButton { symbol: "list-filter"; checked: bar.book.revision >= 0 && bar.book.hasFilter(); onClicked: bar.host.run("filter") }
        TButton { symbol: "table"; checked: bar.host.table.index !== undefined; onClicked: bar.host.run("table") }
    }

    Row {
        id: right
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        // «Órdenes»: everything the app does, a few letters away.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: bar.width >= 1080 ? 132 : 30
            height: 26
            radius: 7
            color: commandArea.containsMouse ? Theme.sideSelection : (width > 30 ? Theme.field : "transparent")
            Sym {
                x: parent.width > 30 ? 8 : 7
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                size: parent.width > 30 ? 13 : 16
                color: parent.width > 30 ? Theme.dim : Theme.text
            }
            Text {
                visible: parent.width > 30
                x: 28
                anchors.verticalCenter: parent.verticalCenter
                text: "Órdenes"
                color: Theme.dim
                font.pixelSize: 12
            }
            Text {
                visible: parent.width > 30
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "Ctrl K"
                color: Theme.faint
                font.pixelSize: 11
            }
            MouseArea {
                id: commandArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: bar.host.run("palette")
            }
        }
        Item { width: 4; height: 1 }
        TButton {
            id: share
            symbol: "share"
            onClicked: shareMenu.popup(share, 0, share.height + 4)
        }
        TButton {
            symbol: "panel-right"
            checked: bar.host.showInspector
            onClicked: bar.host.run("inspector")
        }
    }

    FMenu {
        id: sumMenu
        FMenuItem { text: "Suma"; onTriggered: bar.host.run("sum") }
        FMenuItem { text: "Media"; onTriggered: bar.host.run("average") }
        FMenuItem { text: "Recuento"; onTriggered: bar.host.run("count") }
        FMenuItem { text: "Máximo"; onTriggered: bar.host.run("max") }
        FMenuItem { text: "Mínimo"; onTriggered: bar.host.run("min") }
    }
    FMenu {
        id: chartMenu
        FMenuItem { text: "Columnas"; onTriggered: bar.host.run("chart-column") }
        FMenuItem { text: "Barras"; onTriggered: bar.host.run("chart-bar") }
        FMenuItem { text: "Líneas"; onTriggered: bar.host.run("chart-line") }
        FMenuItem { text: "Área"; onTriggered: bar.host.run("chart-area") }
        FMenuItem { text: "Circular"; onTriggered: bar.host.run("chart-pie") }
        FMenuItem { text: "Dispersión"; onTriggered: bar.host.run("chart-scatter") }
    }
    FMenu {
        id: shareMenu
        FMenuItem { text: "Guardar"; onTriggered: bar.host.run("save") }
        FMenuItem { text: "Guardar como…"; onTriggered: bar.host.run("save-as") }
        FSeparator {}
        FMenuItem { text: "Exportar como PDF…"; onTriggered: bar.host.run("export-pdf") }
        FMenuItem { text: "Exportar la hoja como CSV…"; onTriggered: bar.host.run("export-csv") }
        FMenuItem { text: "Exportar una copia en Excel…"; onTriggered: bar.host.run("export-xlsx") }
        FSeparator {}
        FMenuItem { text: "Versiones…"; onTriggered: bar.host.run("versions") }
        FMenuItem { text: "Abrir otro documento…"; onTriggered: bar.host.run("open") }
    }
}
