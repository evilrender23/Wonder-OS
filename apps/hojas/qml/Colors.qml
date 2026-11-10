pragma Singleton
import QtQuick

/// The swatches of the inspector, and the colouring of a formula.
QtObject {
    readonly property var inks: ["", "#1d1d1f", "#6e6e73", "#ffffff", "#d70015", "#b25000", "#1f9d55", "#0a7aff", "#5e5ce6", "#ad3fb8"]
    readonly property var fills: ["", "#f2f2f7", "#d1d1d6", "#1d1d1f", "#fde2e1", "#ffe8d1", "#fff4c2", "#dcf5e3", "#d5f0fa", "#dbeaff", "#e6e3ff", "#f6dcf8",
        "#ff6b61", "#ff9f0a", "#ffd60a", "#30b566", "#32ade6", "#0a84ff", "#5e5ce6", "#bf5af2"]

    function html(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/ /g, "&nbsp;")
    }
    /// A formula as styled text, each reference in the colour of its range.
    function formula(text, references, ink) {
        let out = ""
        let at = 0
        for (const ref of references) {
            if (ref.start < at)
                continue
            out += html(text.slice(at, ref.start))
            out += "<font color=\"" + ref.color + "\">" + html(text.slice(ref.start, ref.start + ref.length)) + "</font>"
            at = ref.start + ref.length
        }
        return out + html(text.slice(at))
    }
}
