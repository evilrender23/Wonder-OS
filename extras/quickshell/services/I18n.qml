pragma Singleton
import QtQuick
import Quickshell
import "I18nEn.js" as En

// UI language. The shell's source strings are Spanish; any other session
// language gets the English text from I18nEn.js. WONDERLAND_LANG=es|en
// overrides the locale.
Singleton {
    id: root

    readonly property string lang: {
        const forced = Quickshell.env("WONDERLAND_LANG")
        if (forced === "es" || forced === "en") return forced
        return Qt.locale().name.startsWith("es") ? "es" : "en"
    }

    function tr(s) {
        if (lang === "es") return s
        const t = En.table[s]
        return typeof t === "string" ? t : s
    }
}
