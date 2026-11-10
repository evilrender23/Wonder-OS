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
    /// Bases' own colour: what is selected, and what can be pressed.
    readonly property color tint: dark ? "#ff5c77" : "#c2334d"
    readonly property color hover: dark ? "#19ffffff" : "#12000000"
    readonly property color sideSelection: dark ? "#2affffff" : "#1f000000"
    /// The headers of a grid and every other row of a list.
    readonly property color rest: dark ? "#0affffff" : "#07000000"
    readonly property color field: dark ? "#19ffffff" : "#0f000000"
    readonly property color menu: dark ? "#2c2c2e" : "#f9f9f9"
    /// The row and the cell a grid is on.
    readonly property color selection: Qt.alpha(tint, dark ? 0.22 : 0.12)
    readonly property string mono: "monospace"

    function symbol(name, color) {
        return "image://symbol/" + name + "/" + String(color).slice(1)
    }
    /// The glyph of what a column holds.
    function kindSymbol(kind) {
        switch (kind) {
        case "integer":
        case "real":
            return "hash"
        case "date":
            return "calendar"
        case "datetime":
            return "clock"
        case "bool":
            return "square-check"
        case "blob":
            return "file"
        default:
            return "type"
        }
    }
    function objectSymbol(kind) {
        return ({table: "table", view: "eye", query: "code", form: "form", report: "file-text", relations: "workflow"})[kind] ?? "file"
    }
}
