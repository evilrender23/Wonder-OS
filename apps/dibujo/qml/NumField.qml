import QtQuick

/// A number to type, with its unit; the arrows and the wheel step it.
Field {
    id: field
    property real value: 0
    property int decimals: 1
    property real step: 1
    property real from: -100000
    property real to: 100000
    property string unit
    /// The selected differ: no number to show until one is typed.
    property bool mixed: false
    signal edited(real value)

    function shown() {
        if (mixed)
            return "—"
        const fixed = Number(value).toFixed(decimals)
        return String(Number(fixed)).replace(".", ",") + (unit === "" ? "" : " " + unit)
    }
    function take(number) {
        if (isNaN(number))
            return
        edited(Math.max(from, Math.min(to, number)))
    }

    implicitWidth: 71
    horizontalAlignment: TextInput.AlignLeft
    onValueChanged: if (!activeFocus) text = shown()
    onUnitChanged: text = shown()
    onMixedChanged: if (!activeFocus) text = shown()
    Component.onCompleted: text = shown()
    onActiveFocusChanged: {
        if (activeFocus)
            selectAll()
        else
            text = shown()
    }
    onEditingFinished: {
        const number = parseFloat(text.replace(",", "."))
        if (!isNaN(number) && (mixed || Math.abs(number - value) > 1e-9))
            take(number)
        text = shown()
    }
    Keys.onUpPressed: take(value + step)
    Keys.onDownPressed: take(value - step)
    WheelHandler {
        enabled: field.activeFocus
        onWheel: event => field.take(field.value + (event.angleDelta.y > 0 ? field.step : -field.step))
    }
}
