pragma Singleton
import QtQuick

QtObject {
    readonly property bool dark: App.dark
    readonly property color window: dark ? "#1e1e1e" : "#f5f5f7"
    readonly property color sidebar: dark ? "#262628" : "#e9e9ec"
    readonly property color card: dark ? "#2a2a2c" : "#ffffff"
    readonly property color text: dark ? "#f2f2f2" : "#1d1d1f"
    readonly property color dim: dark ? "#a0a0a5" : "#6e6e73"
    readonly property color faint: dark ? "#6a6a6e" : "#aeaeb2"
    readonly property color border: dark ? "#3a3a3c" : "#dcdcdc"
    readonly property color accent: dark ? "#0a84ff" : "#0a7aff"
    readonly property color hover: dark ? "#19ffffff" : "#12000000"
    readonly property color selection: dark ? "#2affffff" : "#1f000000"
    readonly property color field: dark ? "#19ffffff" : "#0f000000"
    readonly property color danger: dark ? "#ff6961" : "#d70015"
    readonly property color dangerFill: dark ? "#3a1f1e" : "#fdecea"
    readonly property color cap: dark ? "#3c3c3f" : "#ececef"

    readonly property int radius: 10
    readonly property int small: 12
    readonly property int body: 14
}
