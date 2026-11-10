import QtQuick

/// «Cancelar», «Guardar», «Eliminar» at the foot of a dialog.
Rectangle {
    id: button
    property string text
    /// The one that does what the dialog is for.
    property bool primary: false
    /// The one that cannot be undone.
    property bool danger: false
    property bool active: true
    signal clicked()
    implicitWidth: Math.max(86, label.implicitWidth + 28)
    implicitHeight: 28
    radius: 7
    color: primary ? Theme.accent : (area.containsMouse && active ? Theme.sideSelection : Theme.field)
    opacity: !active ? 0.45 : (area.pressed ? 0.8 : 1)
    Text {
        id: label
        anchors.centerIn: parent
        text: button.text
        color: button.primary ? "#ffffff" : (button.danger ? Theme.tint : Theme.text)
        font.pixelSize: 13
        font.weight: button.primary ? Font.DemiBold : Font.Normal
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: if (button.active) button.clicked()
    }
}
