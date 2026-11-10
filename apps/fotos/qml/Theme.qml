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
    /// Fotos' own colour: what is chosen, what is loved, what can be pressed.
    readonly property color tint: dark ? "#8e8cff" : "#5856d6"
    readonly property color love: dark ? "#ff5a6e" : "#fa233b"
    /// Behind a picture that has not arrived yet.
    readonly property color plate: dark ? "#2c2c2f" : "#ececef"
    readonly property color hover: dark ? "#19ffffff" : "#12000000"
    readonly property color sideSelection: dark ? "#2affffff" : "#1f000000"
    readonly property color field: dark ? "#19ffffff" : "#0f000000"
    readonly property color menu: dark ? "#2c2c2e" : "#f9f9f9"
    /// Behind one picture shown alone.
    readonly property color stage: dark ? "#141415" : "#f4f4f6"

    function symbol(name, color) {
        return "image://symbol/" + name + "/" + String(color).slice(1)
    }
    /// The address of a file's small picture. `stamp` changes when the file does.
    function thumb(path, stamp) {
        return path ? "image://thumb/" + stamp + "/" + encodeURIComponent(path) : ""
    }
    function photo(path, stamp) {
        return path ? "image://photo/" + stamp + "/" + encodeURIComponent(path) : ""
    }
}
