import QtQuick
import ".."
import "../services"

// Theme text with a role: "body", "muted", "caption", "title", "section".
Text {
    property string role: "body"

    color: role === "muted" || role === "caption" ? Theme.p.text3
        : role === "section" ? Theme.p.text2 : Theme.p.text
    font.family: Theme.fontFamily
    font.pixelSize: role === "caption" || role === "section" ? Theme.smallFontSize
        : role === "title" ? Theme.titleFontSize : Theme.fontSize
    font.weight: role === "title" || role === "section" ? Font.DemiBold : Font.Normal
    font.capitalization: role === "section" ? Font.AllUppercase : Font.MixedCase
    font.letterSpacing: role === "section" ? 0.6 : 0
    elide: Text.ElideRight
    wrapMode: Text.NoWrap
}
