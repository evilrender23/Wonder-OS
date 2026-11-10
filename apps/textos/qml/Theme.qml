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
    /// Dividers inside a panel: quieter than a border.
    readonly property color line: dark ? "#2f2f31" : "#e8e8ea"
    readonly property color accent: dark ? "#0a84ff" : "#0a7aff"
    /// Textos' own colour: what is on, and what can be pressed.
    readonly property color tint: dark ? "#4f8cff" : "#2f6fed"
    readonly property color hover: dark ? "#19ffffff" : "#12000000"
    readonly property color sideSelection: dark ? "#2affffff" : "#1f000000"
    /// What the sheets lie on.
    readonly property color desk: dark ? "#161617" : "#e9e9ec"
    /// A floating pill or palette: the menu's colour, a touch more solid.
    readonly property color pill: dark ? "#323234" : "#ffffff"
    readonly property color field: dark ? "#19ffffff" : "#0f000000"
    readonly property color menu: dark ? "#2c2c2e" : "#f9f9f9"

    function symbol(name, color) {
        return "image://symbol/" + name + "/" + String(color).slice(1)
    }
}
