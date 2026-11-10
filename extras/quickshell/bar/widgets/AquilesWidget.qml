import QtQuick
import "../.."
import "../../services"
import "../../components"

// AquilesAI: opens the agent's sheet (Super+A). Lit while a turn is running.
Pill {
    property var bar
    widgetId: "aquiles"
    icon: "sparkles"
    iconColor: Aquiles.busy ? Theme.p.accent : Theme.p.text2
    tooltip: Aquiles.busy ? `AquilesAI: ${Aquiles.status || I18n.tr("trabajando…")}` : "AquilesAI (Super+A)"
    onClicked: Popups.aquilesRequested()
}
