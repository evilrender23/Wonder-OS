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
    /// The lines inside a panel: there are many, so they are quieter than a border.
    readonly property color line: dark ? "#2f2f31" : "#e8e8ea"
    readonly property color accent: dark ? "#0a84ff" : "#0a7aff"
    /// Fórmulas' own colour: the selection, the equation in hand, what can be pressed.
    readonly property color tint: dark ? "#8e8cff" : "#5b5bd6"
    readonly property color hover: dark ? "#19ffffff" : "#12000000"
    readonly property color sideSelection: dark ? "#2affffff" : "#1f000000"
    readonly property color field: dark ? "#19ffffff" : "#0f000000"
    readonly property color menu: dark ? "#2c2c2e" : "#f9f9f9"
    /// The endless canvas, and the dots of its grid.
    readonly property color canvas: dark ? "#19191b" : "#f1f2f4"
    readonly property color canvasDots: dark ? "#30ffffff" : "#2c000000"
    /// What floats over the canvas: the tools, the pill by the selection, the minimap.
    readonly property color floating: dark ? "#f02c2c2e" : "#f7ffffff"
    readonly property color floatingBorder: dark ? "#4a4a4c" : "#d4d4d8"

    function symbol(name, color) {
        return "image://symbol/" + name + "/" + String(color).slice(1)
    }
}
