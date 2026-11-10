pragma Singleton
import QtQuick
import Quickshell
import "services"

// Hermes Agent Desktop's default "nous" palette (light and dark), the same
// values extras/themes/hermes gives the compositor. Light values are the
// app's tokens composited to opaque over its chrome; dark values are the
// theme table's own. The short names from before the palette (bg, fg,
// border, …) are kept so older components follow it without edits.
Singleton {
    id: theme

    readonly property bool dark: Settings.data.themeMode === "dark"

    readonly property var lightPalette: ({
        chrome: "#f8f9fe", sidebar: "#f3f7ff", surface: "#fdfdfd", elevated: "#ffffff",
        text: "#242528", text2: "#515255", text3: "#7e7f83", text4: "#a7a8ac",
        stroke1: "#abc0ec", stroke2: "#c3d1f0", stroke3: "#d5dff4", stroke4: "#e2e9f8",
        hover: "#e7ecf7", active: "#d9e2f3", track: "#dce5f6",
        accent: "#0053fd", accentHover: "#1f68fd", accentSoft: "#e6eeff", onAccent: "#fcfcfc",
        red: "#cf2d56", orange: "#db704b", yellow: "#c08532", green: "#1f8a65", purple: "#9e94d5",
        shadow: "#1a2b4d"
    })
    readonly property var darkPalette: ({
        chrome: "#0d1117", sidebar: "#010409", surface: "#161b22", elevated: "#161b22",
        text: "#e6edf3", text2: "#b1bac4", text3: "#7d8590", text4: "#565e68",
        stroke1: "#3e5480", stroke2: "#30363d", stroke3: "#262c34", stroke4: "#1c2128",
        hover: "#17243a", active: "#1d2e4f", track: "#21262d",
        accent: "#4a84fe", accentHover: "#6a99fe", accentSoft: "#17243a", onAccent: "#0d1117",
        red: "#f85149", orange: "#db7c4b", yellow: "#d29922", green: "#3fb950", purple: "#bc8cff",
        shadow: "#000000"
    })
    readonly property var p: dark ? darkPalette : lightPalette

    // Hermes renders system-ui, which fontconfig resolves to the desktop sans.
    readonly property string fontFamily: "Noto Sans"
    readonly property string monoFamily: "Adwaita Mono"

    readonly property int fontSize: 13
    readonly property int smallFontSize: 11
    readonly property int titleFontSize: 15
    readonly property int popupRadius: 8
    // macOS menu-bar metrics: a slim bar, flat 22 px hit targets.
    readonly property int barHeight: 30
    readonly property int pillHeight: 22
    readonly property int iconSize: 15
    readonly property int labelGap: 6
    readonly property int sectionGap: 16
    readonly property int animMs: Settings.data.reduceMotion ? 0 : 160

    // Short names from before the palette, still used across the shell.
    readonly property int radius: 10
    readonly property color bg: p.chrome
    readonly property color border: p.stroke2
    readonly property color fg: p.text
    readonly property color track: p.track
    readonly property real dim: 0.62
    readonly property real hoverOpacity: dark ? 0.12 : 0.07

    // The text colour at alpha `a`: the menu bar's hover/open highlight, which
    // reads on any bar tint (solid or glass) the way macOS' does.
    function veil(a) {
        const c = Qt.darker(p.text, 1.0)   // palette entries are strings
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    // Liquid glass. `glassy(c, solid)` is `c` at the glass opacity while the
    // glass is on, and at `solid` (the surface's own opacity) while it is off.
    // Only for surfaces the compositor frosts (a blur window rule), or the
    // desktop shows through unblurred.
    readonly property bool glass: Settings.data.glass
    readonly property real glassOpacity: Math.max(0.3, Math.min(1, Settings.data.glassOpacity))
    function glassy(c, solid) {
        const a = glass ? glassOpacity * (dark ? 1 : 1.12) : solid
        const col = Qt.darker(c, 1.0)   // palette entries are strings
        return Qt.rgba(col.r, col.g, col.b, Math.min(1, a))
    }
}
