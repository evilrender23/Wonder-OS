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
    /// The lines inside the panels: there are many, so they are quieter than a border.
    readonly property color line: dark ? "#2f2f31" : "#e8e8ea"
    readonly property color accent: dark ? "#0a84ff" : "#0a7aff"
    /// Diapositivas' own colour: the slide in hand, and what is switched on.
    readonly property color tint: dark ? "#ff9f0a" : "#f08c00"
    readonly property color hover: dark ? "#19ffffff" : "#12000000"
    readonly property color sideSelection: dark ? "#2affffff" : "#1f000000"
    readonly property color field: dark ? "#19ffffff" : "#0f000000"
    readonly property color menu: dark ? "#2c2c2e" : "#f9f9f9"
    /// The rim of a colour sample, seen on both a white one and a black one.
    readonly property color swatch: dark ? "#5a5a5e" : "#c4c4c8"
    /// The table the slide lies on.
    readonly property color desk: dark ? "#161617" : "#e9e9eb"
    /// The frame of what is selected on the slide, and the alignment guides.
    readonly property color guide: "#ff2d92"

    /// A shortcut as the keyboard names it here.
    function keys(sequence) {
        const names = {Shift: "Mayús", Del: "Supr", PgUp: "Re Pág", PgDown: "Av Pág", Return: "Intro", Backspace: "Retroceso",
            Up: "↑", Down: "↓", Left: "←", Right: "→"}
        return String(sequence || "").split("+").map(key => names[key] || key).join("+").replace(/\+$/, "++").replace("++++", "++")
    }
    function symbol(name, color) {
        return "image://symbol/" + name + "/" + String(color).slice(1)
    }
    /// The tint as a quiet background.
    function wash(color) {
        return Qt.alpha(color, dark ? 0.30 : 0.18)
    }
}
