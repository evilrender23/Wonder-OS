import QtQuick
import "../.."
import "../../services"
import "../../components"

// The magnifier: opens Spotlight (Super+Espacio).
Pill {
    property var bar
    widgetId: "spotlight"
    icon: "search"
    tooltip: "Spotlight (Super+Espacio)"
    onClicked: Popups.spotlightRequested()
}
