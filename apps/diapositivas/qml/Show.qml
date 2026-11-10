import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window

/// The talk itself: the slides one after another, for the audience, or with
/// the speaker's view around them — notes, what comes next, the clock and the time gone.
Rectangle {
    id: show
    property var ui
    readonly property var deck: App.deck
    /// The slides that are shown, in order.
    property var order: []
    property int at: 0
    /// How many builds of the slide have come in.
    property int step: 0
    property int before: -1
    property bool speaker: false
    property int seconds: 0
    property bool paused: false
    property bool over: false
    property string clock: ""
    readonly property int index: order.length > 0 ? order[at] : 0
    readonly property var info: { deck.revision; return deck.slide(index) }
    readonly property bool busy: change.running || build.running
    /// The slides have a screen of their own when there is a second one.
    readonly property bool twoScreens: speaker && App.screens > 1 && !App.testing

    function begin(fromHere, withSpeaker) {
        order = deck.shown()
        if (order.length === 0)
            return false
        let start = 0
        if (fromHere) {
            for (let i = 0; i < order.length; ++i) {
                if (order[i] <= deck.current)
                    start = i
            }
        }
        at = start
        step = 0
        before = -1
        over = false
        speaker = withSpeaker
        seconds = 0
        paused = false
        incoming.x = 0
        incoming.opacity = 1
        outgoing.opacity = 0
        layer.visible = false
        tick()
        forceActiveFocus()
        return true
    }
    function finish() {
        change.complete()
        build.complete()
        deck.current = index
        ui.endShow()
    }
    function next() {
        if (busy) {
            change.complete()
            build.complete()
            return
        }
        if (over) {
            finish()
        } else if (step < (info.builds || 0)) {
            const id = info.buildIds[step]
            const kind = (deck.items(index).find(item => item.id === id) || {}).anim
            layer.only = id
            layer.visible = true
            layer.opacity = kind === "slide" ? 0 : 1
            layer.y = kind === "slide" ? incoming.height * 0.06 : 0
            if (kind === "slide")
                build.start()
            else
                build.done()
        } else if (at < order.length - 1) {
            go(at + 1, true)
        } else {
            over = true
        }
    }
    function back() {
        change.complete()
        build.complete()
        if (over)
            over = false
        else if (step > 0)
            step -= 1
        else if (at > 0)
            go(at - 1, false)
    }
    function go(to, animated) {
        before = index
        at = Math.max(0, Math.min(order.length - 1, to))
        over = false
        // Going back shows the slide whole, as it was left.
        step = animated ? 0 : (deck.slide(order[at]).builds || 0)
        const kind = animated ? (deck.slide(order[at]).transition || "") : ""
        incoming.x = 0
        incoming.opacity = 1
        outgoing.x = 0
        outgoing.opacity = 0
        if (kind === "dissolve") {
            outgoing.opacity = 1
            incoming.opacity = 0
            change.kind = kind
            change.start()
        } else if (kind === "fade") {
            outgoing.opacity = 1
            incoming.opacity = 0
            change.kind = kind
            change.start()
        } else if (kind === "push") {
            outgoing.opacity = 1
            incoming.x = frame.width
            change.kind = kind
            change.start()
        }
    }
    function tick() {
        clock = Qt.formatTime(new Date(), "HH:mm")
    }
    function span(total) {
        const pad = number => (number < 10 ? "0" : "") + number
        return (total >= 3600 ? Math.floor(total / 3600) + ":" : "") + pad(Math.floor(total / 60) % 60) + ":" + pad(total % 60)
    }

    color: speaker ? "#161617" : "#000000"
    focus: visible
    Keys.onPressed: event => {
        event.accepted = true
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Q)
            finish()
        else if ([Qt.Key_Right, Qt.Key_Down, Qt.Key_Space, Qt.Key_PageDown, Qt.Key_Return, Qt.Key_Enter, Qt.Key_N].indexOf(event.key) >= 0)
            next()
        else if ([Qt.Key_Left, Qt.Key_Up, Qt.Key_PageUp, Qt.Key_Backspace].indexOf(event.key) >= 0)
            back()
        else if (event.key === Qt.Key_Home)
            go(0, false)
        else if (event.key === Qt.Key_End)
            go(order.length - 1, false)
        else if (event.key === Qt.Key_P)
            speaker = !speaker
        else if (event.key === Qt.Key_R)
            seconds = 0
        else if (event.key === Qt.Key_T)
            paused = !paused
        else
            event.accepted = false
    }

    Timer {
        interval: 1000
        repeat: true
        running: show.visible
        onTriggered: {
            if (!show.paused)
                show.seconds += 1
            show.tick()
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => mouse.button === Qt.RightButton ? show.back() : show.next()
    }

    // --- The slide, for whoever is looking ---
    Item {
        id: frame
        readonly property real room: show.speaker ? show.width * 0.62 - 36 : show.width
        readonly property real tall: show.speaker ? show.height - 150 : show.height
        readonly property real k: Math.min(room / show.deck.width, tall / show.deck.height)
        x: show.speaker ? 24 : Math.round((show.width - width) / 2)
        y: show.speaker ? 76 : Math.round((show.height - height) / 2)
        width: Math.round(show.deck.width * k)
        height: Math.round(show.deck.height * k)
        clip: true
        Rectangle { anchors.fill: parent; color: "#000000" }
        SlideView {
            id: outgoing
            width: parent.width
            height: parent.height
            deck: show.deck
            index: Math.max(0, show.before)
            opacity: 0
        }
        SlideView {
            id: incoming
            width: parent.width
            height: parent.height
            deck: show.deck
            index: show.index
            step: show.step
        }
        // The build that is coming in, on a layer of its own.
        SlideView {
            id: layer
            visible: false
            width: parent.width
            height: parent.height
            deck: show.deck
            index: show.index
        }
        Rectangle {
            anchors.fill: parent
            visible: show.over
            color: "#000000"
            Text {
                anchors.centerIn: parent
                text: "Fin de la presentación"
                color: "#8e8e93"
                font.pixelSize: Math.max(13, parent.height * 0.035)
            }
        }
    }
    SequentialAnimation {
        id: change
        property string kind
        ParallelAnimation {
            NumberAnimation { target: outgoing; property: "opacity"; to: 0; duration: change.kind === "push" ? 1 : (change.kind === "fade" ? 320 : 450); easing.type: Easing.InOutQuad }
            NumberAnimation { target: incoming; property: "opacity"; to: change.kind === "fade" ? 0 : 1; duration: change.kind === "dissolve" ? 450 : 1; easing.type: Easing.InOutQuad }
            NumberAnimation { target: incoming; property: "x"; to: 0; duration: change.kind === "push" ? 480 : 1; easing.type: Easing.InOutCubic }
            NumberAnimation { target: outgoing; property: "x"; to: change.kind === "push" ? -frame.width : 0; duration: change.kind === "push" ? 480 : 1; easing.type: Easing.InOutCubic }
        }
        NumberAnimation { target: incoming; property: "opacity"; to: 1; duration: change.kind === "fade" ? 320 : 1; easing.type: Easing.InOutQuad }
        ScriptAction { script: { outgoing.opacity = 0; outgoing.x = 0 } }
    }
    SequentialAnimation {
        id: build
        function done() {
            show.step += 1
            layer.visible = false
        }
        ParallelAnimation {
            NumberAnimation { target: layer; property: "opacity"; to: 1; duration: 380; easing.type: Easing.OutCubic }
            NumberAnimation { target: layer; property: "y"; to: 0; duration: 380; easing.type: Easing.OutCubic }
        }
        ScriptAction { script: build.done() }
    }

    // --- What only the speaker sees ---
    Item {
        visible: show.speaker
        anchors.fill: parent
        Row {
            x: 24
            y: 20
            spacing: 14
            Text {
                text: show.clock
                color: "#f2f2f2"
                font.pixelSize: 30
                font.weight: Font.DemiBold
            }
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 1; height: 26; color: "#3a3a3c" }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: show.span(show.seconds)
                color: show.paused ? "#8e8e93" : "#ff9f0a"
                font.pixelSize: 30
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 26
                radius: 6
                color: pauseArea.containsMouse ? "#2affffff" : "transparent"
                Sym { anchors.centerIn: parent; name: show.paused ? "play" : "pause"; size: 16; color: "#f2f2f2" }
                MouseArea { id: pauseArea; anchors.fill: parent; hoverEnabled: true; onClicked: show.paused = !show.paused }
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 26
                radius: 6
                color: resetArea.containsMouse ? "#2affffff" : "transparent"
                Sym { anchors.centerIn: parent; name: "rotate-ccw"; size: 16; color: "#f2f2f2" }
                MouseArea { id: resetArea; anchors.fill: parent; hoverEnabled: true; onClicked: show.seconds = 0 }
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 24
            y: 24
            spacing: 14
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Diapositiva " + (show.at + 1) + " de " + show.order.length + ((show.info.builds || 0) > 0 ? " · paso " + show.step + " de " + show.info.builds : "")
                color: "#a0a0a5"
                font.pixelSize: 14
            }
            Rectangle {
                width: label.implicitWidth + 24
                height: 28
                radius: 7
                color: endArea.containsMouse ? "#3affffff" : "#24ffffff"
                Text { id: label; anchors.centerIn: parent; text: "Terminar"; color: "#f2f2f2"; font.pixelSize: 13 }
                MouseArea { id: endArea; anchors.fill: parent; hoverEnabled: true; onClicked: show.finish() }
            }
        }
        Rectangle {
            x: frame.x - 2
            y: frame.y - 2
            z: -1
            width: frame.width + 4
            height: frame.height + 4
            color: "transparent"
            border.color: "#ff9f0a"
            border.width: 2
            radius: 2
        }
        Row {
            x: frame.x
            y: frame.y + frame.height + 16
            spacing: 6
            Rectangle {
                width: 44
                height: 34
                radius: 8
                color: prevArea.containsMouse ? "#3affffff" : "#24ffffff"
                Sym { anchors.centerIn: parent; name: "chevron-left"; size: 18; color: "#f2f2f2" }
                MouseArea { id: prevArea; anchors.fill: parent; hoverEnabled: true; onClicked: show.back() }
            }
            Rectangle {
                width: 44
                height: 34
                radius: 8
                color: nextArea.containsMouse ? "#3affffff" : "#24ffffff"
                Sym { anchors.centerIn: parent; name: "chevron-right"; size: 18; color: "#f2f2f2" }
                MouseArea { id: nextArea; anchors.fill: parent; hoverEnabled: true; onClicked: show.next() }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: 10
                text: "← → para pasar · P quita esta vista · R pone el cronómetro a cero · Esc termina"
                color: "#6a6a6e"
                font.pixelSize: 12
            }
        }
        Column {
            id: aside
            x: frame.x + frame.room + 36
            y: 76
            width: show.width - x - 24
            spacing: 10
            Text { text: "A continuación"; color: "#a0a0a5"; font.pixelSize: 11; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.4 }
            Rectangle {
                width: Math.min(parent.width, 420)
                height: Math.round(width * show.deck.height / show.deck.width)
                color: "#000000"
                border.color: "#3a3a3c"
                SlideView {
                    anchors.fill: parent
                    anchors.margins: 1
                    visible: show.at < show.order.length - 1
                    deck: show.deck
                    index: show.at < show.order.length - 1 ? show.order[show.at + 1] : 0
                }
                Text {
                    visible: show.at >= show.order.length - 1
                    anchors.centerIn: parent
                    text: "Es la última"
                    color: "#6a6a6e"
                    font.pixelSize: 13
                }
            }
            Item { width: 1; height: 6 }
            Text { text: "Notas"; color: "#a0a0a5"; font.pixelSize: 11; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.4 }
            Flickable {
                width: parent.width
                height: Math.max(40, show.height - aside.y - y - 24)
                contentHeight: notes.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { width: 7 }
                Text {
                    id: notes
                    width: parent.width - 10
                    text: (show.info.notes || "") === "" ? "Esta diapositiva no tiene notas." : show.info.notes
                    color: (show.info.notes || "") === "" ? "#6a6a6e" : "#f2f2f2"
                    font.pixelSize: 18
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // --- The second screen: only the slide ---
    Loader {
        active: show.visible && show.twoScreens
        sourceComponent: Window {
            screen: App.audienceScreen(show.ui)
            visibility: Window.FullScreen
            visible: true
            color: "#000000"
            title: "Diapositivas"
            SlideView {
                anchors.centerIn: parent
                readonly property real k: Math.min(parent.width / show.deck.width, parent.height / show.deck.height)
                width: show.deck.width * k
                height: show.deck.height * k
                deck: show.deck
                index: show.index
                step: show.step
                visible: !show.over
            }
        }
    }
}
