import QtQuick
import QtQuick.Controls.Basic

/// A day, typed (10/11/2026, 10/11) or picked from a small month.
Item {
    id: control
    /// Julian day number; 0 is «none», which only an `optional` field allows.
    property int day: 0
    property bool optional: false
    property string placeholder
    signal edited(int day)
    implicitWidth: 132
    implicitHeight: 28

    function openPicker() {
        const from = App.parts(day > 0 ? day : App.today)
        mini.year = from.year
        mini.month = from.month
        picker.open()
    }

    Field {
        id: field
        anchors.fill: parent
        rightPadding: 30
        placeholderText: control.placeholder
        text: App.dateText(control.day)
        onTextEdited: wrong = !(control.optional && text.trim() === "") && App.parseDate(text) === 0
        onEditingFinished: {
            const typed = text.trim() === "" && control.optional ? 0 : App.parseDate(text)
            wrong = false
            if ((typed > 0 || (control.optional && text.trim() === "")) && typed !== control.day)
                control.edited(typed)
            text = Qt.binding(() => App.dateText(control.day))
        }
        onActiveFocusChanged: if (activeFocus) selectAll()
    }
    TButton {
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        width: 26
        height: 24
        symbol: "calendar"
        size: 14
        tone: Theme.dim
        onClicked: control.openPicker()
    }
    Popup {
        id: picker
        y: control.height + 4
        padding: 10
        background: Rectangle {
            radius: 10
            color: Theme.menu
            border.color: Theme.border
        }
        contentItem: MiniMonth {
            id: mini
            selected: control.day
            onShifted: months => {
                const first = App.parts(App.monthStart(year, month + months))
                year = first.year
                month = first.month
            }
            onPicked: day => {
                picker.close()
                if (day !== control.day)
                    control.edited(day)
            }
        }
    }
}
