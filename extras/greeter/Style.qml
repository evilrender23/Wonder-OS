pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// Look and settings of the greeter. The palette is the shell's (extras/
// quickshell/Theme.qml) reduced to what a login screen over a live wallpaper
// needs: text straight on the wallpaper, and veils of the text colour for
// every control, so it reads on any tint the wallpaper has.
Singleton {
    id: style

    // /etc/wonderland-greeter/greeter.json, written by install.sh:
    //   theme   "dark" | "light"   palette (match the wallpaper's light_mode)
    //   lang    "es" | "en" | …    language of the strings and the date
    //   screen  connector name     monitor that shows the form ("" = largest)
    //   clock24 bool               24-hour clock
    readonly property string configPath:
        Quickshell.env("WONDERLAND_GREETER_CONFIG") || "/etc/wonderland-greeter/greeter.json"
    property var config: ({})

    FileView {
        path: style.configPath
        blockLoading: true
        printErrors: false
        watchChanges: true
        onFileChanged: reload()
        onLoaded: {
            try { style.config = JSON.parse(text()) } catch (e) { style.config = {} }
        }
    }

    readonly property bool dark: (config.theme || "dark") !== "light"
    readonly property string lang: config.lang || Qt.locale().name.split("_")[0] || "en"
    readonly property bool clock24: config.clock24 !== false
    readonly property string screenName: config.screen || ""

    readonly property color text: dark ? "#e6edf3" : "#242528"
    readonly property color text2: dark ? "#b1bac4" : "#515255"
    readonly property color text3: dark ? "#7d8590" : "#7e7f83"
    readonly property color accent: dark ? "#4a84fe" : "#0053fd"
    readonly property color onAccent: dark ? "#0d1117" : "#fcfcfc"
    readonly property color red: dark ? "#ff7b72" : "#cf2d56"
    readonly property color menu: dark ? "#161b22" : "#ffffff"

    readonly property string fontFamily: "Noto Sans"
    readonly property int fontSize: 13
    readonly property int smallFontSize: 11
    readonly property int iconSize: 15
    readonly property int animMs: 160

    // The text colour at alpha `a`: fills, hairlines and hover highlights.
    function veil(a) {
        return Qt.rgba(text.r, text.g, text.b, a)
    }

    readonly property var strings: ({
        es: {
            password: "Contraseña", username: "Usuario", wrong: "Contraseña incorrecta",
            other: "Otro usuario", session: "Sesión", suspend: "Suspender",
            restart: "Reiniciar", shutdown: "Apagar", unavailable: "greetd no está disponible",
            noSession: "No hay ninguna sesión instalada", starting: "Entrando…",
            date: "dddd, d 'de' MMMM"
        },
        en: {
            password: "Password", username: "User name", wrong: "Wrong password",
            other: "Other user", session: "Session", suspend: "Suspend",
            restart: "Restart", shutdown: "Shut down", unavailable: "greetd is not available",
            noSession: "No session is installed", starting: "Signing in…",
            date: "dddd, MMMM d"
        }
    })
    function tr(key) {
        return (strings[lang] || strings.en)[key]
    }
}
