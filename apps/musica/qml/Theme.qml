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
    readonly property color accent: dark ? "#0a84ff" : "#0a7aff"
    /// Música's own colour: what plays, what is loved, what can be pressed to play.
    readonly property color tint: dark ? "#fa586a" : "#fa233b"
    readonly property color plate: dark ? "#3c3c3f" : "#e3e3e6"
    readonly property color hover: dark ? "#19ffffff" : "#12000000"
    readonly property color sideSelection: dark ? "#2affffff" : "#1f000000"
    readonly property color stripe: dark ? "#08ffffff" : "#06000000"
    readonly property color field: dark ? "#19ffffff" : "#0f000000"
    readonly property color menu: dark ? "#2c2c2e" : "#f9f9f9"
    readonly property color selection: dark ? "#33fa586a" : "#22fa233b"

    function symbol(name, color) {
        return "image://symbol/" + name + "/" + String(color).slice(1)
    }
}
