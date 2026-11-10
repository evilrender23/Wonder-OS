import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtMultimedia

ApplicationWindow {
    id: win

    readonly property var player: App.player
    readonly property bool watching: player.hasMedia
    readonly property bool full: visibility === Window.FullScreen
    property bool queueOpen: false
    /// The bars over the picture: there while the pointer moves, gone while it rests.
    property bool chrome: true
    /// fit, fill or stretch.
    property string fill: "fit"
    /// The time at the right: what is left instead of how long it is.
    property bool showRemaining: App.setting("remaining", false)
    readonly property var rates: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
    readonly property bool menuOpen: subtitleMenu.visible || audioMenu.visible || rateMenu.visible || fillMenu.visible || contextMenu.visible
    readonly property var recent: App.history.revision >= 0 ? App.history.recent(30) : []

    width: App.testing ? App.testSize.width : App.setting("width", 1040)
    height: App.testing ? App.testSize.height : App.setting("height", 640)
    minimumWidth: 560
    minimumHeight: 360
    visible: true
    title: watching ? player.title + " — Películas" : "Películas"
    color: watching ? Theme.stage : (App.glass ? "transparent" : Theme.window)

    onClosing: {
        if (App.testing)
            return
        if (visibility === Window.Windowed) {
            App.setSetting("width", width)
            App.setSetting("height", height)
        }
        App.setSetting("remaining", showRemaining)
    }
    Component.onCompleted: {
        player.videoOutput = output
        if (App.testing)
            testStep.start()
    }
    onWatchingChanged: {
        if (!watching) {
            queueOpen = false
            fill = "fit"
            if (full)
                showNormal()
        }
        wake()
    }

    Connections {
        target: App
        function onRaiseRequested() {
            win.raise()
            win.requestActivate()
        }
        function onFailed(message) {
            win.say(message)
        }
    }
    Connections {
        target: win.player
        function onResumed(position) {
            win.say("Sigue desde " + App.time(position))
        }
        function onPlayingChanged() {
            win.wake()
        }
    }

    // --- What it does ------------------------------------------------------

    function wake() {
        chrome = true
        hide.restart()
    }
    function say(message) {
        toast.text = message
        toast.opacity = 1
        toastOut.restart()
    }
    function toggleFull() {
        if (full)
            showNormal()
        else
            showFullScreen()
    }
    function changeVolume(delta) {
        player.volume = Math.round((player.volume + delta) * 20) / 20
        say(player.muted ? "Silencio" : "Volumen " + Math.round(player.volume * 100) + " %")
    }
    function toggleMute() {
        player.muted = !player.muted
        say(player.muted ? "Silencio" : "Volumen " + Math.round(player.volume * 100) + " %")
    }
    function rateName(rate) {
        return rate === 1 ? "Normal" : String(rate).replace(".", ",") + "×"
    }
    function setRate(rate) {
        player.rate = rate
        say("Velocidad " + (rate === 1 ? "normal" : rateName(rate)))
    }
    function stepRate(direction) {
        let at = 0
        for (let i = 0; i < rates.length; ++i) {
            if (Math.abs(rates[i] - player.rate) < Math.abs(rates[at] - player.rate))
                at = i
        }
        setRate(rates[Math.max(0, Math.min(rates.length - 1, at + direction))])
    }
    function step(ms) {
        player.step(ms)
        wake()
    }
    function snapshot() {
        const path = player.snapshot()
        say(path !== "" ? "Captura guardada en Imágenes" : "No se pudo guardar la captura")
    }
    function cycleSubtitles() {
        if (player.subtitleChoices.length < 2) {
            say("Sin subtítulos")
            return
        }
        player.cycleSubtitles()
        const chosen = player.subtitleChoices.find(choice => choice.key === player.subtitle)
        say("Subtítulos: " + (chosen ? chosen.label : "Desactivados"))
    }
    readonly property string volumeSymbol: player.muted || player.volume === 0 ? "volume-x" : (player.volume < 0.34 ? "volume" : (player.volume < 0.67 ? "volume-1" : "volume-2"))

    Timer {
        id: hide
        interval: 2600
        onTriggered: {
            if (win.watching && win.player.playing && !win.menuOpen && !bottomHover.hovered && !topHover.hovered && !seek.dragging && !volumeSeek.dragging)
                win.chrome = false
        }
    }

    // --- Keys --------------------------------------------------------------

    Shortcut { sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequence: "Ctrl+Shift+O"; onActivated: folderDialog.open() }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: win.close() }
    Shortcut { sequences: ["F", "F11"]; enabled: win.watching; onActivated: win.toggleFull() }
    Shortcut {
        sequence: "Escape"
        enabled: win.watching
        onActivated: {
            if (win.full)
                win.showNormal()
            else if (win.queueOpen)
                win.queueOpen = false
        }
    }
    Shortcut { sequences: ["Space", "K"]; enabled: win.watching; onActivated: win.player.toggle() }
    Shortcut { sequence: "Left"; enabled: win.watching; onActivated: win.step(-10000) }
    Shortcut { sequence: "Right"; enabled: win.watching; onActivated: win.step(10000) }
    Shortcut { sequence: "Shift+Left"; enabled: win.watching; onActivated: win.step(-60000) }
    Shortcut { sequence: "Shift+Right"; enabled: win.watching; onActivated: win.step(60000) }
    Shortcut { sequence: "Home"; enabled: win.watching; onActivated: win.player.seek(0) }
    Shortcut { sequence: "Up"; enabled: win.watching; onActivated: win.changeVolume(0.05) }
    Shortcut { sequence: "Down"; enabled: win.watching; onActivated: win.changeVolume(-0.05) }
    Shortcut { sequence: "M"; enabled: win.watching; onActivated: win.toggleMute() }
    Shortcut { sequences: ["N", "Ctrl+Right"]; enabled: win.watching && win.player.canNext; onActivated: win.player.next() }
    Shortcut { sequences: ["P", "Ctrl+Left"]; enabled: win.watching; onActivated: win.player.previous() }
    Shortcut { sequence: "L"; enabled: win.watching; onActivated: win.queueOpen = !win.queueOpen }
    Shortcut { sequence: "V"; enabled: win.watching; onActivated: win.cycleSubtitles() }
    Shortcut { sequence: "S"; enabled: win.watching; onActivated: win.snapshot() }
    Shortcut { sequence: "]"; enabled: win.watching; onActivated: win.stepRate(1) }
    Shortcut { sequence: "["; enabled: win.watching; onActivated: win.stepRate(-1) }
    Shortcut { sequence: "Backspace"; enabled: win.watching; onActivated: win.setRate(1) }
    Shortcut { sequence: "Ctrl+W"; enabled: win.watching; onActivated: win.player.close() }

    // --- Dialogs -----------------------------------------------------------

    readonly property var mediaFilters: ["Vídeo y sonido (*.mp4 *.m4v *.mkv *.webm *.mov *.avi *.wmv *.flv *.mpg *.mpeg *.ts *.m2ts *.ogv *.3gp *.vob *.mp3 *.flac *.ogg *.opus *.m4a *.aac *.wav *.wma)", "Todos los archivos (*)"]
    FileDialog {
        id: fileDialog
        title: "Abrir"
        fileMode: FileDialog.OpenFiles
        nameFilters: win.mediaFilters
        onAccepted: App.open(selectedFiles)
    }
    FolderDialog {
        id: folderDialog
        title: "Abrir carpeta"
        onAccepted: App.open([selectedFolder])
    }
    FileDialog {
        id: addDialog
        title: "Añadir a la lista"
        fileMode: FileDialog.OpenFiles
        nameFilters: win.mediaFilters
        onAccepted: App.enqueue(selectedFiles)
    }
    FileDialog {
        id: subtitleDialog
        title: "Subtítulos"
        nameFilters: ["Subtítulos (*.srt *.vtt *.ass *.ssa)", "Todos los archivos (*)"]
        onAccepted: win.player.useSubtitleFile(selectedFile)
    }

    // --- Menus -------------------------------------------------------------

    FMenu {
        id: subtitleMenu
        Instantiator {
            model: win.player.subtitleChoices
            delegate: FMenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: win.player.subtitle === modelData.key
                onTriggered: win.player.subtitle = modelData.key
            }
            onObjectAdded: (index, object) => subtitleMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => subtitleMenu.removeItem(object)
        }
        FSeparator {}
        FMenuItem {
            text: "Añadir archivo de subtítulos…"
            onTriggered: subtitleDialog.open()
        }
    }
    FMenu {
        id: audioMenu
        Instantiator {
            model: win.player.audioTracks
            delegate: FMenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: win.player.audioTrack === modelData.index
                onTriggered: win.player.audioTrack = modelData.index
            }
            onObjectAdded: (index, object) => audioMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => audioMenu.removeItem(object)
        }
    }
    FMenu {
        id: rateMenu
        Instantiator {
            model: win.rates
            delegate: FMenuItem {
                required property var modelData
                text: win.rateName(modelData)
                checkable: true
                checked: Math.abs(win.player.rate - modelData) < 0.01
                onTriggered: win.setRate(modelData)
            }
            onObjectAdded: (index, object) => rateMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => rateMenu.removeItem(object)
        }
    }
    FMenu {
        id: fillMenu
        FMenuItem { text: "Ajustar a la ventana"; checkable: true; checked: win.fill === "fit"; onTriggered: win.fill = "fit" }
        FMenuItem { text: "Llenar la ventana"; checkable: true; checked: win.fill === "fill"; onTriggered: win.fill = "fill" }
        FMenuItem { text: "Estirar"; checkable: true; checked: win.fill === "stretch"; onTriggered: win.fill = "stretch" }
    }
    FMenu {
        id: contextMenu
        FMenuItem { text: win.player.playing ? "Pausa" : "Reproducir"; onTriggered: win.player.toggle() }
        FMenuItem { text: win.full ? "Salir de pantalla completa" : "Pantalla completa"; onTriggered: win.toggleFull() }
        FSeparator {}
        FMenuItem { text: "Subtítulos…"; onTriggered: subtitleMenu.popup() }
        FMenuItem { text: "Pista de sonido…"; enabled: win.player.audioTracks.length > 1; onTriggered: audioMenu.popup() }
        FMenuItem { text: "Velocidad…"; onTriggered: rateMenu.popup() }
        FMenuItem { text: "Tamaño de la imagen…"; enabled: win.player.hasVideo; onTriggered: fillMenu.popup() }
        FSeparator {}
        FMenuItem { text: "Guardar captura"; enabled: win.player.hasVideo; onTriggered: win.snapshot() }
        FMenuItem { text: "Mostrar en el Finder"; onTriggered: App.reveal(win.player.path) }
        FSeparator {}
        FMenuItem { text: "Abrir…"; onTriggered: fileDialog.open() }
        FMenuItem { text: "Cerrar la película"; onTriggered: win.player.close() }
    }

    // --- The first page ----------------------------------------------------

    Welcome {
        anchors.fill: parent
        visible: !win.watching
        recent: win.recent
        onOpenFiles: fileDialog.open()
        onOpenFolder: folderDialog.open()
        onChosen: path => App.openPath(path)
        onForgotten: path => App.history.forget(path)
        onCleared: App.history.clear()
    }

    // --- The picture -------------------------------------------------------

    Item {
        id: stage
        anchors.fill: parent
        visible: win.watching

        HoverHandler {
            onPointChanged: win.wake()
        }

        VideoOutput {
            id: output
            anchors.fill: parent
            anchors.rightMargin: queue.shown * queue.width
            fillMode: win.fill === "fill" ? VideoOutput.PreserveAspectCrop : (win.fill === "stretch" ? VideoOutput.Stretch : VideoOutput.PreserveAspectFit)
        }

        // Only sound: something to look at.
        Column {
            visible: win.watching && !win.player.hasVideo
            anchors.centerIn: output
            spacing: 14
            Sym {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "audio-lines"
                size: 72
                color: Theme.inkFaint
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, output.width - 80)
                text: win.player.title
                color: Theme.inkDim
                font.pixelSize: 18
                elide: Text.ElideMiddle
            }
        }

        MouseArea {
            anchors.fill: output
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: win.chrome ? Qt.ArrowCursor : Qt.BlankCursor
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton)
                    contextMenu.popup()
                else
                    win.player.toggle()
            }
            onDoubleClicked: mouse => {
                if (mouse.button === Qt.LeftButton)
                    win.toggleFull()
            }
            onWheel: wheel => win.changeVolume(wheel.angleDelta.y > 0 ? 0.05 : -0.05)
        }

        Text {
            id: subtitle
            visible: text !== ""
            anchors.horizontalCenter: output.horizontalCenter
            anchors.bottom: output.bottom
            anchors.bottomMargin: Math.round(output.height * 0.05) + (bottomBar.opacity > 0.5 ? bottomBar.height - 30 : 0)
            width: Math.round(output.width * 0.84)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: win.player.subtitleText
            textFormat: Text.PlainText
            color: "#ffffff"
            style: Text.Outline
            styleColor: "#000000"
            font.pixelSize: Math.max(16, Math.min(46, Math.round(output.height / 19)))
            font.weight: Font.Medium
            Behavior on anchors.bottomMargin { NumberAnimation { duration: 160 } }
        }

        // What just changed, said once.
        Rectangle {
            id: toast
            property alias text: toastText.text
            anchors.horizontalCenter: output.horizontalCenter
            y: 56
            width: toastText.implicitWidth + 28
            height: 32
            radius: 16
            color: "#cc1a1a1c"
            opacity: 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            Text {
                id: toastText
                anchors.centerIn: parent
                color: Theme.ink
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Timer {
                id: toastOut
                interval: 1700
                onTriggered: toast.opacity = 0
            }
        }

        // --- Over the top: what is on ---
        Rectangle {
            id: topBar
            width: output.width
            height: 64
            opacity: win.chrome ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            gradient: Gradient {
                GradientStop { position: 0; color: "#b0000000" }
                GradientStop { position: 1; color: "#00000000" }
            }
            HoverHandler { id: topHover }
            TButton {
                id: closeButton
                x: 10
                y: 10
                onStage: true
                symbol: "chevron-left"
                size: 18
                tip: "Cerrar la película"
                onClicked: win.player.close()
            }
            Text {
                anchors.left: closeButton.right
                anchors.leftMargin: 6
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: closeButton.verticalCenter
                text: win.player.title
                color: Theme.ink
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
            }
        }

        // --- Over the bottom: where it is, and what to do with it ---
        Rectangle {
            id: bottomBar
            anchors.bottom: parent.bottom
            width: output.width
            height: 92
            opacity: win.chrome ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
            gradient: Gradient {
                GradientStop { position: 0; color: "#00000000" }
                GradientStop { position: 0.55; color: "#b0000000" }
                GradientStop { position: 1; color: "#d9000000" }
            }
            HoverHandler { id: bottomHover }
            // The bar is not the picture: a click between two buttons does not pause.
            MouseArea {
                anchors.fill: parent
                anchors.topMargin: 24
                acceptedButtons: Qt.AllButtons
                onWheel: wheel => wheel.accepted = true
            }

            readonly property real shownPosition: seek.dragging ? seek.dragValue * win.player.duration : win.player.position

            Text {
                id: elapsed
                x: 16
                anchors.verticalCenter: seek.verticalCenter
                text: App.time(bottomBar.shownPosition)
                color: Theme.ink
                font.pixelSize: 12
                font.features: { "tnum": 1 }
            }
            Seek {
                id: seek
                anchors.left: elapsed.right
                anchors.leftMargin: 12
                anchors.right: total.left
                anchors.rightMargin: 12
                y: 30
                active: win.player.duration > 0
                fill: Theme.inkTint
                value: win.player.duration > 0 ? win.player.position / win.player.duration : 0
                wheelStep: win.player.duration > 0 ? Math.min(0.05, 10000 / win.player.duration) : 0.05
                onReleased: value => win.player.seek(Math.round(value * win.player.duration))
            }
            Rectangle {
                visible: seek.active && (seek.hovered || seek.dragging)
                x: Math.max(8, Math.min(bottomBar.width - width - 8, seek.x + (seek.dragging ? seek.dragValue : seek.hoverValue) * seek.width - width / 2))
                y: seek.y - height - 8
                width: bubbleText.implicitWidth + 14
                height: 22
                radius: 6
                color: "#e61a1a1c"
                Text {
                    id: bubbleText
                    anchors.centerIn: parent
                    text: App.time((seek.dragging ? seek.dragValue : seek.hoverValue) * win.player.duration)
                    color: Theme.ink
                    font.pixelSize: 11
                    font.features: { "tnum": 1 }
                }
            }
            Text {
                id: total
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: seek.verticalCenter
                text: win.showRemaining && win.player.duration > 0 ? "−" + App.time(win.player.duration - bottomBar.shownPosition) : App.time(win.player.duration)
                color: Theme.inkDim
                font.pixelSize: 12
                font.features: { "tnum": 1 }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    onClicked: win.showRemaining = !win.showRemaining
                }
            }

            Row {
                id: transport
                x: 10
                y: 54
                spacing: 2
                TButton {
                    onStage: true
                    symbol: "prev-fill"
                    tip: "Anterior (P)"
                    onClicked: win.player.previous()
                }
                TButton {
                    onStage: true
                    symbol: "rewind"
                    visible: bottomBar.width >= 640
                    tip: "Atrás 10 s (←)"
                    onClicked: win.step(-10000)
                }
                TButton {
                    onStage: true
                    width: 38
                    height: 30
                    y: -2
                    size: 20
                    symbol: win.player.playing ? "pause-fill" : "play-fill"
                    tip: win.player.playing ? "Pausa (Espacio)" : "Reproducir (Espacio)"
                    onClicked: win.player.toggle()
                }
                TButton {
                    onStage: true
                    symbol: "fast-forward"
                    visible: bottomBar.width >= 640
                    tip: "Adelante 10 s (→)"
                    onClicked: win.step(10000)
                }
                TButton {
                    onStage: true
                    symbol: "next-fill"
                    active: win.player.canNext
                    tip: "Siguiente (N)"
                    onClicked: win.player.next()
                }
                Item { width: 8; height: 1 }
                TButton {
                    onStage: true
                    symbol: win.volumeSymbol
                    tip: win.player.muted ? "Activar el sonido (M)" : "Silenciar (M)"
                    onClicked: win.toggleMute()
                }
                Seek {
                    id: volumeSeek
                    anchors.verticalCenter: parent.verticalCenter
                    width: 84
                    visible: bottomBar.width >= 560
                    value: win.player.muted ? 0 : win.player.volume
                    onMoved: value => win.player.volume = value
                }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                y: 54
                spacing: 2
                TButton {
                    onStage: true
                    symbol: "captions"
                    checked: win.player.subtitle !== ""
                    tip: "Subtítulos (V)"
                    onClicked: subtitleMenu.popup()
                }
                TButton {
                    onStage: true
                    symbol: "audio-lines"
                    visible: win.player.audioTracks.length > 1 && bottomBar.width >= 420
                    tip: "Pista de sonido"
                    onClicked: audioMenu.popup()
                }
                TButton {
                    onStage: true
                    symbol: "gauge"
                    visible: bottomBar.width >= 420
                    checked: Math.abs(win.player.rate - 1) > 0.01
                    tip: "Velocidad ([ y ])"
                    onClicked: rateMenu.popup()
                }
                TButton {
                    onStage: true
                    symbol: "proportions"
                    visible: win.player.hasVideo && bottomBar.width >= 640
                    checked: win.fill !== "fit"
                    tip: "Tamaño de la imagen"
                    onClicked: fillMenu.popup()
                }
                TButton {
                    onStage: true
                    symbol: "camera"
                    visible: win.player.hasVideo && bottomBar.width >= 640
                    tip: "Guardar captura (S)"
                    onClicked: win.snapshot()
                }
                TButton {
                    onStage: true
                    symbol: "list-video"
                    checked: win.queueOpen
                    tip: "Lista (L)"
                    onClicked: win.queueOpen = !win.queueOpen
                }
                TButton {
                    onStage: true
                    symbol: win.full ? "minimize" : "maximize"
                    tip: win.full ? "Salir de pantalla completa (F)" : "Pantalla completa (F)"
                    onClicked: win.toggleFull()
                }
            }
        }

        Queue {
            id: queue
            /// 0 closed, 1 open.
            property real shown: win.queueOpen ? 1 : 0
            Behavior on shown { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            width: Math.min(300, Math.round(stage.width * 0.45))
            height: parent.height
            x: parent.width - shown * width
            visible: shown > 0
            playlist: win.player.playlist
            onPlayed: index => win.player.playAt(index)
            onAddRequested: addDialog.open()
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            if (drop.hasUrls)
                App.open(drop.urls)
        }
    }

    // --- Test runs: --shot with --do ---------------------------------------

    function runAction(action) {
        const colon = action.indexOf(":")
        const verb = colon < 0 ? action : action.slice(0, colon)
        const arg = colon < 0 ? "" : action.slice(colon + 1)
        switch (verb) {
        case "pause":
            player.pause()
            break
        case "seek":
            player.seek(Number(arg))
            break
        case "queue":
            queueOpen = true
            break
        case "at":
            player.playAt(Number(arg))
            break
        case "fill":
            fill = arg
            break
        case "rate":
            setRate(Number(arg))
            break
        case "volume":
            player.volume = Number(arg)
            break
        case "mute":
            toggleMute()
            break
        case "subtitle":
            player.subtitle = player.subtitleChoices[Number(arg)].key
            break
        case "repeat":
            player.playlist.repeat = Number(arg)
            break
        case "shuffle":
            player.playlist.shuffle = true
            break
        case "remaining":
            showRemaining = true
            break
        case "hide":
            chrome = false
            break
        case "close":
            player.close()
            break
        case "snapshot":
            snapshot()
            break
        case "menu":
            ({ subtitles: subtitleMenu, audio: audioMenu, rate: rateMenu, fill: fillMenu, context: contextMenu })[arg].popup(win.contentItem, 300, 160)
            break
        }
    }
    Timer {
        id: testStep
        interval: 1400
        onTriggered: {
            for (const action of App.testActions)
                win.runAction(action.trim())
            testShot.start()
        }
    }
    Timer {
        id: testShot
        interval: 1500
        onTriggered: App.saveShot(win)
    }
}
