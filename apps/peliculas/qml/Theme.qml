pragma Singleton
import QtQuick

QtObject {
    readonly property bool dark: App.dark
    readonly property color window: dark ? "#1e1e1e" : "#ffffff"
    /// The first page, frosted when the desktop is.
    readonly property color page: App.glass ? (dark ? "#b31c1c1e" : "#b3f4f4f6") : (dark ? "#1e1e1e" : "#f6f6f7")
    readonly property color text: dark ? "#f2f2f2" : "#1d1d1f"
    readonly property color dim: dark ? "#a0a0a5" : "#6e6e73"
    readonly property color faint: dark ? "#6a6a6e" : "#aeaeb2"
    readonly property color border: dark ? "#3a3a3c" : "#dcdcdc"
    readonly property color accent: dark ? "#0a84ff" : "#0a7aff"
    /// Películas' own colour: what is playing, what can be pressed.
    readonly property color tint: dark ? "#ff8a5c" : "#e4572e"
    readonly property color hover: dark ? "#19ffffff" : "#12000000"
    readonly property color sideSelection: dark ? "#2affffff" : "#1f000000"
    readonly property color field: dark ? "#19ffffff" : "#0f000000"
    readonly property color card: dark ? "#14ffffff" : "#b3ffffff"
    readonly property color menu: dark ? "#2c2c2e" : "#f9f9f9"

    // Over the picture it is always night: the same in light and dark.
    readonly property color stage: "#000000"
    readonly property color ink: "#ffffff"
    readonly property color inkDim: "#b8ffffff"
    readonly property color inkFaint: "#59ffffff"
    readonly property color inkHover: "#26ffffff"
    readonly property color inkField: "#38ffffff"
    readonly property color inkTint: "#ff8a5c"
    readonly property color panel: "#e61a1a1c"

    function symbol(name, color) {
        return "image://symbol/" + name + "/" + String(color).slice(1)
    }
}
