import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Widgets
import "../.."
import "../../services"
import "../../components"
import "../../panels"

// The ☤ mark. Left click: AquilesAI, the desktop agent (aquiles/AquilesPanel.qml).
// Right click: a macOS-style system menu (about, settings, Spotlight, force
// quit, sleep/restart/shut down, lock, log out). Middle click: shell settings
// (theme picker, layout lock, styles), also in the menu and on Super+,.
Pill {
    id: root

    property var bar
    widgetId: "launcher"
    // No `panel`: `togglePanel launcher` stays Spotlight's (spotlight/Spotlight.qml).
    Connections {
        target: Popups
        function onPanelRequested(id) { if (id === "settings" && root.QsWindow.window.screen === Quickshell.screens[0]) settings.toggleFromIpc() }
    }
    active: menu.visible || settings.visible
    tooltip: Aquiles.busy ? `AquilesAI: ${Aquiles.status || I18n.tr("trabajando…")}` : ""
    // With the menu or the settings open, a click on the mark closes them.
    onClicked: {
        if (menu.visible || settings.visible) { menu.close(); settings.close(); return }
        Popups.aquilesRequested()
    }
    onRightClicked: menu.toggle()
    onMiddleClicked: settings.toggle()

    // The caduceus glyph (U+2624) instead of a Lucide icon; Noto Sans lacks
    // it, so Qt falls back to a symbols font.
    Text {
        text: "☤"
        color: Theme.p.accent
        font.pixelSize: Theme.iconSize + 4
        font.family: Theme.fontFamily
        Layout.alignment: Qt.AlignVCenter
    }

    function run(cmd) { Quickshell.execDetached(["sh", "-c", cmd]) }

    BarPanel {
        id: menu
        target: root
        contentWidth: 250
        padding: 5
        menu: true
        onVisibleChanged: armed.which = ""

        // Restart / shut down / log out ask once more (click again).
        QtObject { id: armed; property string which: "" }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            MenuRow {
                text: "AquilesAI…"
                shortcut: "❖ A"
                onTriggered: { menu.close(); Popups.aquilesRequested() }
            }
            MenuSeparator {}
            MenuRow {
                text: I18n.tr("Acerca de este equipo")
                onTriggered: { menu.close(); Popups.aboutRequested() }
            }
            MenuSeparator {}
            MenuRow {
                text: I18n.tr("Ajustes del sistema…")
                onTriggered: { menu.close(); root.run("command -v wonderland-sistema >/dev/null && exec wonderland-sistema; exec systemsettings") }
            }
            MenuRow {
                text: I18n.tr("Ajustes de la barra…")
                shortcut: "❖ ,"
                onTriggered: { menu.close(); settings.open() }
            }
            MenuRow {
                text: I18n.tr("Pantallas…")
                shortcut: "❖ P"
                onTriggered: { menu.close(); Popups.displaysRequested() }
            }
            MenuRow {
                text: "Spotlight…"
                shortcut: I18n.tr("❖ Espacio")
                onTriggered: { menu.close(); Popups.spotlightRequested() }
            }
            MenuSeparator {}
            MenuRow {
                text: I18n.tr("Forzar salida…")
                onTriggered: { menu.close(); root.run("plasma-systemmonitor") }
            }
            MenuSeparator {}
            MenuRow {
                text: I18n.tr("Reposo")
                onTriggered: { menu.close(); Session.suspend() }
            }
            MenuRow {
                text: armed.which === "reboot" ? I18n.tr("¿Reiniciar? Pulsa otra vez") : I18n.tr("Reiniciar…")
                armed: armed.which === "reboot"
                onTriggered: {
                    if (armed.which !== "reboot") { armed.which = "reboot"; return }
                    menu.close(); Session.reboot()
                }
            }
            MenuRow {
                text: armed.which === "poweroff" ? I18n.tr("¿Apagar? Pulsa otra vez") : I18n.tr("Apagar…")
                armed: armed.which === "poweroff"
                onTriggered: {
                    if (armed.which !== "poweroff") { armed.which = "poweroff"; return }
                    menu.close(); Session.poweroff()
                }
            }
            MenuSeparator {}
            MenuRow {
                text: I18n.tr("Bloquear pantalla")
                onTriggered: { menu.close(); Session.lock() }
            }
            MenuRow {
                readonly property string user: Quickshell.env("USER") || ""
                text: armed.which === "logout" ? I18n.tr("¿Cerrar sesión? Pulsa otra vez") : I18n.tr("Cerrar sesión de %1…").arg(user)
                armed: armed.which === "logout"
                onTriggered: {
                    if (armed.which !== "logout") { armed.which = "logout"; return }
                    menu.close(); Session.logout()
                }
            }
        }
    }

    SettingsPanel { id: settings; target: root }
}
