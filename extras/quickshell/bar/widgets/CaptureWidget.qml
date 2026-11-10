import QtQuick
import "../.."
import "../../services"
import "../../components"

// The camera: opens the capture overlay. While a recording runs it turns into
// its elapsed time, and a click stops it.
Pill {
    property var bar
    widgetId: "capture"
    icon: Capture.recording ? "circle-dot" : "camera"
    iconColor: Capture.recording ? Theme.p.red : Theme.p.text
    textColor: Theme.p.red
    text: {
        if (!Capture.recording) return ""
        const s = Capture.recSeconds, m = Math.floor(s / 60), r = s % 60
        return `${m < 10 ? "0" : ""}${m}:${r < 10 ? "0" : ""}${r}`
    }
    active: Capture.open
    tooltip: Capture.recording ? "Detener la grabación" : "Capturar o grabar la pantalla (Impr Pant)"
    onClicked: Capture.recording ? Capture.stop() : Capture.toggle()
    onRightClicked: if (!Capture.recording) Capture.show("record")
}
