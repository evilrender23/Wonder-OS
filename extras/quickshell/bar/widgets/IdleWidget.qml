import QtQuick
import Quickshell
import "../.."
import "../../services"
import "../../components"

// Keep-awake toggle (the bar carries the IdleInhibitor, see Bar.qml).
Pill {
    id: root

    property var bar
    widgetId: "idle"
    icon: "coffee"
    iconColor: Session.idleInhibited ? Theme.p.accent : Theme.p.text3
    active: Session.idleInhibited
    tooltip: Session.idleInhibited ? "Staying awake" : "Keep awake"
    onClicked: Session.idleInhibited = !Session.idleInhibited
}
