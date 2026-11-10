import QtQuick

/// A number to type, with a unit after it.
Field {
    id: box
    property real value: 0
    property string unit
    property int decimals: 0
    signal edited(real number)
    function shown() {
        return Number(value).toFixed(decimals).replace(".", ",") + unit
    }
    implicitWidth: 64
    horizontalAlignment: TextInput.AlignRight
    text: shown()
    onValueChanged: text = shown()
    onEditingFinished: {
        const number = parseFloat(text.replace(",", "."))
        if (!isNaN(number) && number !== value)
            edited(number)
        text = Qt.binding(shown)
    }
}
