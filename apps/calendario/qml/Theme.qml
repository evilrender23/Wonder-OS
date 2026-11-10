pragma Singleton
import QtQuick

QtObject {
    readonly property bool dark: App.dark
    readonly property color window: dark ? "#1e1e1e" : "#ffffff"
    readonly property color chrome: dark ? "#2a2a2c" : "#f6f6f6"
    readonly property color sidebar: App.glass ? (dark ? "#99202022" : "#99ececec") : (dark ? "#262628" : "#ececec")
    readonly property color text: dark ? "#f2f2f2" : "#1d1d1f"
    readonly property color dim: dark ? "#a0a0a5" : "#6e6e73"
    readonly property color faint: dark ? "#6a6a6e" : "#aeaeb2"
    readonly property color border: dark ? "#3a3a3c" : "#dcdcdc"
    /// The lines of the grids: there are many, so they are quieter than a border.
    readonly property color line: dark ? "#2f2f31" : "#e8e8ea"
    readonly property color accent: dark ? "#0a84ff" : "#0a7aff"
    /// Calendario's own colour: today, and what can be pressed.
    readonly property color tint: dark ? "#ff453a" : "#ff3b30"
    readonly property color hover: dark ? "#19ffffff" : "#12000000"
    readonly property color sideSelection: dark ? "#2affffff" : "#1f000000"
    /// Saturdays and Sundays, and the days of another month.
    readonly property color rest: dark ? "#0affffff" : "#07000000"
    readonly property color field: dark ? "#19ffffff" : "#0f000000"
    readonly property color menu: dark ? "#2c2c2e" : "#f9f9f9"

    function symbol(name, color) {
        return "image://symbol/" + name + "/" + String(color).slice(1)
    }
    /// The colour of a calendar as the background of one of its events.
    function wash(color) {
        return Qt.alpha(color, dark ? 0.30 : 0.18)
    }
    /// And as the ink of its title: readable on the wash in light and dark.
    function ink(color) {
        return dark ? Qt.lighter(color, 1.45) : Qt.darker(color, 1.7)
    }
}
