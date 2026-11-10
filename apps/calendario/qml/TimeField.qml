import QtQuick

/// An hour of the day, typed however it comes: 9, 9:30, 0930, 9 pm.
Field {
    id: field
    /// Minutes since midnight.
    property int minute: 0
    signal edited(int minute)
    implicitWidth: 64
    horizontalAlignment: TextInput.AlignHCenter
    text: App.timeText(minute)
    onMinuteChanged: text = App.timeText(minute)
    onTextEdited: wrong = App.parseTime(text) < 0
    onEditingFinished: {
        const typed = App.parseTime(text)
        wrong = false
        if (typed >= 0 && typed !== minute)
            edited(typed)
        text = Qt.binding(() => App.timeText(field.minute))
    }
    onActiveFocusChanged: if (activeFocus) selectAll()
}
