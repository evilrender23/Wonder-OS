import QtQuick
import QtQuick.Layouts

Item {
    id: root

    required property var device
    required property int index
    /// Yours (paired or blocked): it has a page of details.
    property bool known: false
    signal details()

    width: parent ? parent.width : 0
    height: 58

    Rectangle {
        anchors.fill: parent
        color: hover.hovered ? Theme.hover : "transparent"
    }
    Rectangle {
        visible: root.index > 0
        height: 1
        color: Theme.border
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 62 }
    }
    HoverHandler { id: hover }
    TapHandler {
        onDoubleTapped: {
            if (!root.device.busy && !root.device.blocked && !root.device.connected)
                Bt.connectTo(root.device.address)
        }
    }

    RowLayout {
        anchors { fill: parent; leftMargin: 14; rightMargin: 12 }
        spacing: 12

        Rectangle {
            implicitWidth: 36
            implicitHeight: 36
            radius: 18
            color: root.device.connected ? Theme.accent : Theme.plate
            Sym {
                anchors.centerIn: parent
                name: root.device.symbol
                size: 18
                color: root.device.connected ? "#ffffff" : Theme.text
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                text: root.device.name
                color: Theme.text
                font.pixelSize: Theme.body
                elide: Text.ElideRight
                opacity: root.device.blocked ? 0.5 : 1
            }
            Text {
                Layout.fillWidth: true
                text: root.device.status
                color: Theme.dim
                font.pixelSize: Theme.small
                elide: Text.ElideRight
            }
        }
        Spinner {
            visible: root.device.busy
        }
        TButton {
            visible: !root.device.busy && !root.device.blocked
            text: root.device.connected ? qsTr("Desconectar") : qsTr("Conectar")
            onClicked: root.device.connected ? Bt.disconnectFrom(root.device.address) : Bt.connectTo(root.device.address)
        }
        TButton {
            visible: root.known
            implicitWidth: 30
            leftPadding: 0
            rightPadding: 0
            tip: qsTr("Detalles")
            Accessible.name: qsTr("Detalles de %1").arg(root.device.name)
            onClicked: root.details()
            Sym {
                anchors.centerIn: parent
                name: "info"
                size: 16
                color: Theme.dim
            }
        }
    }
}
