//@ pragma UseQApplication
import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Services.Greetd

// Wonderland's login screen: a greetd greeter. The compositor (driftwm, with
// the greeter's own config) draws the live wallpaper; this draws the clock,
// the user, the password field, the session picker and the power buttons on
// one monitor, straight on the wallpaper.
ShellRoot {
    id: root

    // ── Data ────────────────────────────────────────────────────────────
    property var users: []          // [{name, fullName, avatar}]
    property var sessions: []       // [{id, name, exec, desktopNames}]
    property int userIndex: 0
    property int sessionIndex: 0
    property bool manualUser: false // typing a user name instead of picking one
    property string typedUser: ""
    property bool loaded: false
    property var last: ({})         // {user, session} of the previous login

    readonly property var user: users[userIndex] || null
    readonly property var session: sessions[sessionIndex] || null
    readonly property string userName: manualUser || !user ? typedUser : user.name

    // ── Authentication state ────────────────────────────────────────────
    property bool busy: false        // waiting for greetd
    property bool hasSecret: false   // a typed password waits for PAM's prompt
    property string secret: ""
    property bool prompting: false   // PAM asked something beyond the password
    property string promptText: ""
    property bool promptEcho: false
    property string message: ""
    property bool messageIsError: false
    property bool leaving: false

    readonly property string statePath:
        (Quickshell.env("XDG_STATE_HOME") || "/var/lib/wonderland-greeter/state")
        + "/wonderland-greeter/last.json"

    signal failed()

    function fail(text) {
        busy = false
        hasSecret = false
        secret = ""
        prompting = false
        message = text
        messageIsError = true
        failed()
    }

    // Enter in the field: start a login, or answer what PAM asked.
    function submit(text) {
        if (busy || leaving) return
        if (prompting) {
            prompting = false
            busy = true
            Greetd.respond(text)
            return
        }
        if (manualUser && typedUser === "") {
            typedUser = text.trim()
            return
        }
        if (userName === "") return
        if (!Greetd.available) {
            fail(Style.tr("unavailable"))
            return
        }
        if (!session) {
            fail(Style.tr("noSession"))
            return
        }
        message = ""
        secret = text
        hasSecret = true
        busy = true
        Greetd.createSession(userName)
    }

    function pickUser(index) {
        if (busy || leaving) return
        if (Greetd.state !== GreetdState.Inactive) Greetd.cancelSession()
        manualUser = index < 0
        typedUser = ""
        if (index >= 0) userIndex = index
        prompting = false
        message = ""
    }

    function power(verb) {
        if (leaving) return
        Quickshell.execDetached(["systemctl", verb])
    }

    Connections {
        target: Greetd

        function onAuthMessage(text, error, responseRequired, echoResponse) {
            if (!responseRequired) {
                root.message = text
                root.messageIsError = error
                return
            }
            // The first hidden prompt is the password that is already typed.
            if (!echoResponse && root.hasSecret) {
                const s = root.secret
                root.secret = ""
                root.hasSecret = false
                Greetd.respond(s)
                return
            }
            // Anything else (a one-time code, a new password…) is asked as is.
            root.secret = ""
            root.hasSecret = false
            root.promptText = text.replace(/[:\s]+$/, "")
            root.promptEcho = echoResponse
            root.prompting = true
            root.busy = false
        }

        function onAuthFailure(text) {
            root.fail(Style.tr("wrong"))
        }

        function onError(text) {
            root.fail(text)
        }

        function onReadyToLaunch() {
            root.secret = ""
            root.hasSecret = false
            root.message = ""
            root.leaving = true
            const s = root.session
            Quickshell.execDetached(["sh", "-c",
                'mkdir -p "$(dirname "$1")" && printf %s "$2" > "$1.tmp" && mv "$1.tmp" "$1"',
                "sh", root.statePath, JSON.stringify({ user: root.userName, session: s.id })])
            const env = ["XDG_SESSION_TYPE=wayland", "XDG_SESSION_DESKTOP=" + s.id]
            if (s.desktopNames) env.push("XDG_CURRENT_DESKTOP=" + s.desktopNames)
            Greetd.launch(s.exec.trim().split(/\s+/), env, false)
        }

        // greetd starts the session once the greeter is gone: fade, then quit.
        function onLaunched() {
            quitTimer.start()
        }
    }

    Timer {
        id: quitTimer
        interval: 420
        onTriggered: Qt.quit()
    }

    FileView {
        id: lastFile
        path: root.statePath
        blockLoading: true
        printErrors: false
        onLoaded: {
            try { root.last = JSON.parse(text()) } catch (e) { root.last = {} }
        }
    }

    Process {
        command: [Quickshell.env("WONDERLAND_GREETER_INFO") || Quickshell.shellDir + "/scripts/greeter-info"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                let info = {}
                try { info = JSON.parse(text) } catch (e) { console.warn("greeter-info:", e) }
                const users = info.users || []
                const sessions = info.sessions || []
                // The distro's own session first, unless another was used last.
                let s = sessions.findIndex(x => x.id === root.last.session)
                if (s < 0) s = sessions.findIndex(x => x.id === "wonderlandwm")
                let u = users.findIndex(x => x.name === root.last.user)
                root.users = users
                root.sessions = sessions
                root.sessionIndex = Math.max(0, s)
                root.userIndex = Math.max(0, u)
                root.manualUser = users.length === 0
                root.loaded = true
            }
        }
    }

    // The monitor with the form: the configured one, else the largest.
    readonly property var formScreen: {
        const all = Quickshell.screens
        let best = null
        let area = -1
        for (let i = 0; i < all.length; i++) {
            const s = all[i]
            if (Style.screenName !== "" && s.name === Style.screenName) return s
            const a = s.width * s.height * s.devicePixelRatio * s.devicePixelRatio
            if (a > area) {
                area = a
                best = s
            }
        }
        return best
    }

    SystemClock {
        id: clock
        precision: SystemClock.Minutes
    }

    PanelWindow {
        id: win

        screen: root.formScreen
        color: "transparent"
        WlrLayershell.namespace: "wonderland-greeter"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
        exclusionMode: ExclusionMode.Ignore
        anchors { top: true; bottom: true; left: true; right: true }

        Item {
            id: stage
            anchors.fill: parent
            opacity: 0
            scale: root.leaving ? 1.03 : 1.0

            states: [
                State { name: "in"; when: root.loaded && !root.leaving; PropertyChanges { stage.opacity: 1 } },
                State { name: "out"; when: root.leaving; PropertyChanges { stage.opacity: 0 } }
            ]
            transitions: [
                Transition { to: "in"; NumberAnimation { property: "opacity"; duration: 700; easing.type: Easing.OutCubic } },
                Transition { to: "out"; NumberAnimation { property: "opacity"; duration: 380; easing.type: Easing.InCubic } }
            ]
            Behavior on scale { NumberAnimation { duration: 380; easing.type: Easing.InCubic } }

            // A click anywhere gives the keyboard back to the field.
            MouseArea {
                anchors.fill: parent
                onPressed: {
                    sessionMenu.open = false
                    field.forceActiveFocus()
                }
            }

            // ── Clock ───────────────────────────────────────────────────
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Math.round(parent.height * 0.13)
                spacing: 2

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: Qt.formatTime(clock.date, Style.clock24 ? "HH:mm" : "h:mm")
                    color: Style.text
                    font.family: Style.fontFamily
                    font.pixelSize: Math.round(Math.min(stage.height * 0.115, 132))
                    font.weight: Font.ExtraLight
                    font.letterSpacing: -1
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: {
                        const s = clock.date.toLocaleDateString(Qt.locale(Style.lang), Style.tr("date"))
                        return s.charAt(0).toUpperCase() + s.slice(1)
                    }
                    color: Style.text2
                    font.family: Style.fontFamily
                    font.pixelSize: 17
                }
            }

            // ── User and password ───────────────────────────────────────
            Column {
                id: form
                anchors.horizontalCenter: parent.horizontalCenter
                y: Math.round(parent.height * 0.52)
                spacing: 0

                property real shake: 0
                transform: Translate { x: form.shake }

                SequentialAnimation {
                    id: shakeAnim
                    NumberAnimation { target: form; property: "shake"; to: -12; duration: 50 }
                    NumberAnimation { target: form; property: "shake"; to: 10; duration: 70 }
                    NumberAnimation { target: form; property: "shake"; to: -7; duration: 70 }
                    NumberAnimation { target: form; property: "shake"; to: 4; duration: 60 }
                    NumberAnimation { target: form; property: "shake"; to: 0; duration: 50 }
                }
                Connections {
                    target: root
                    function onFailed() {
                        field.text = ""
                        field.forceActiveFocus()
                        shakeAnim.restart()
                    }
                }

                Avatar {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 92
                    name: root.userName
                    image: root.manualUser || !root.user ? "" : root.user.avatar
                }

                Item { width: 1; height: 14 }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.manualUser
                        ? (root.typedUser || Style.tr("other"))
                        : root.user ? (root.user.fullName || root.user.name) : ""
                    color: Style.text
                    font.family: Style.fontFamily
                    font.pixelSize: 19
                    font.weight: Font.Medium
                }

                Item { width: 1; height: 18 }

                // The field: a pill of the text colour at low alpha.
                Rectangle {
                    id: pill
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 264
                    height: 38
                    radius: height / 2
                    color: Style.veil(field.activeFocus ? 0.13 : 0.09)
                    border.width: 1
                    border.color: Style.veil(field.activeFocus ? 0.34 : 0.18)
                    opacity: root.busy ? 0.6 : 1.0
                    Behavior on color { ColorAnimation { duration: Style.animMs } }
                    Behavior on border.color { ColorAnimation { duration: Style.animMs } }
                    Behavior on opacity { NumberAnimation { duration: Style.animMs } }

                    readonly property bool secretInput:
                        root.prompting ? !root.promptEcho : !(root.manualUser && root.typedUser === "")

                    TextInput {
                        id: field
                        anchors.left: parent.left
                        anchors.right: go.left
                        anchors.leftMargin: 18
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        focus: true
                        clip: true
                        readOnly: root.busy || root.leaving
                        color: Style.text
                        selectionColor: Style.accent
                        selectedTextColor: Style.onAccent
                        font.family: Style.fontFamily
                        font.pixelSize: 14
                        echoMode: pill.secretInput && !reveal.shown ? TextInput.Password : TextInput.Normal
                        passwordCharacter: "•"
                        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        onAccepted: {
                            const t = text
                            text = ""
                            reveal.shown = false
                            root.submit(t)
                        }
                        Keys.onEscapePressed: {
                            text = ""
                            reveal.shown = false
                            sessionMenu.open = false
                            // Out of a PAM question, or back from «other user».
                            if (root.prompting)
                                root.pickUser(root.manualUser ? -1 : root.userIndex)
                            else if (root.manualUser && root.users.length > 0)
                                root.pickUser(root.userIndex)
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: field.text === ""
                            text: root.busy ? Style.tr("starting")
                                : root.prompting ? root.promptText
                                : root.manualUser && root.typedUser === "" ? Style.tr("username")
                                : Style.tr("password")
                            color: Style.text3
                            font: field.font
                        }
                    }

                    // Show/hide what is typed.
                    Item {
                        id: reveal
                        property bool shown: false
                        anchors.right: go.left
                        anchors.rightMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        height: 26
                        visible: pill.secretInput && field.text !== "" && !root.busy
                        Icon {
                            anchors.centerIn: parent
                            name: reveal.shown ? "eye-off" : "eye"
                            size: 15
                            color: Style.text2
                            opacity: revealArea.containsMouse ? 1.0 : 0.6
                        }
                        MouseArea {
                            id: revealArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: reveal.shown = !reveal.shown
                        }
                    }

                    Rectangle {
                        id: go
                        anchors.right: parent.right
                        anchors.rightMargin: 5
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28
                        height: 28
                        radius: 14
                        color: goArea.containsMouse && !root.busy ? Style.accent : Style.veil(0.14)
                        Behavior on color { ColorAnimation { duration: Style.animMs } }

                        Icon {
                            anchors.centerIn: parent
                            visible: !root.busy
                            name: "chevron-right"
                            size: 16
                            color: goArea.containsMouse ? Style.onAccent : Style.text
                        }
                        // Waiting for greetd: a turning arc.
                        Item {
                            anchors.centerIn: parent
                            width: 14
                            height: 14
                            visible: root.busy
                            Rectangle {
                                anchors.fill: parent
                                radius: 7
                                color: "transparent"
                                border.width: 2
                                border.color: Style.veil(0.25)
                            }
                            Rectangle {
                                width: 4
                                height: 4
                                radius: 2
                                color: Style.text
                                x: 5
                                y: -1
                            }
                            RotationAnimator on rotation {
                                running: root.busy
                                from: 0
                                to: 360
                                duration: 900
                                loops: Animation.Infinite
                            }
                        }
                        MouseArea {
                            id: goArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: field.accepted()
                        }
                    }
                }

                Item { width: 1; height: 12 }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 18
                    text: root.message
                    color: root.messageIsError ? Style.red : Style.text2
                    font.family: Style.fontFamily
                    font.pixelSize: Style.fontSize
                    opacity: root.message !== "" ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Style.animMs } }
                }

                Item { width: 1; height: 22 }

                // The other accounts, and «other user» to type a name.
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 18
                    visible: root.users.length !== 1

                    Repeater {
                        model: root.users
                        delegate: Item {
                            id: chip
                            required property var modelData
                            required property int index
                            visible: root.manualUser || index !== root.userIndex
                            width: visible ? 84 : 0
                            height: 62
                            opacity: chipArea.containsMouse ? 1.0 : 0.7
                            Behavior on opacity { NumberAnimation { duration: Style.animMs } }
                            Avatar {
                                anchors.horizontalCenter: parent.horizontalCenter
                                size: 38
                                name: chip.modelData.name
                                image: chip.modelData.avatar
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: chip.modelData.fullName || chip.modelData.name
                                color: Style.text2
                                font.family: Style.fontFamily
                                font.pixelSize: Style.smallFontSize
                            }
                            MouseArea {
                                id: chipArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.pickUser(chip.index)
                                    field.text = ""
                                    field.forceActiveFocus()
                                }
                            }
                        }
                    }

                    Item {
                        visible: !root.manualUser
                        width: visible ? 84 : 0
                        height: 62
                        opacity: otherArea.containsMouse ? 1.0 : 0.7
                        Behavior on opacity { NumberAnimation { duration: Style.animMs } }
                        Avatar {
                            anchors.horizontalCenter: parent.horizontalCenter
                            size: 38
                            name: "+"
                        }
                        Text {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: Style.tr("other")
                            color: Style.text2
                            font.family: Style.fontFamily
                            font.pixelSize: Style.smallFontSize
                        }
                        MouseArea {
                            id: otherArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.pickUser(-1)
                                field.text = ""
                                field.forceActiveFocus()
                            }
                        }
                    }
                }
            }

            // ── Session picker (bottom left) ────────────────────────────
            Item {
                id: sessionMenu
                property bool open: false
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 28
                anchors.bottomMargin: 24
                width: sessionPill.width
                height: sessionPill.height
                visible: root.sessions.length > 0

                Rectangle {
                    id: sessionPill
                    width: sessionRow.implicitWidth + 26
                    height: 34
                    radius: 17
                    color: Style.veil(sessionArea.pressed ? 0.20 : sessionArea.containsMouse || sessionMenu.open ? 0.12 : 0.0)
                    Behavior on color { ColorAnimation { duration: Style.animMs } }

                    Row {
                        id: sessionRow
                        anchors.centerIn: parent
                        spacing: 8
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "monitor"
                            size: 15
                            opacity: 0.8
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.session ? root.session.name : ""
                            color: Style.text
                            opacity: 0.86
                            font.family: Style.fontFamily
                            font.pixelSize: Style.fontSize
                        }
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.sessions.length > 1
                            name: "chevron-down"
                            size: 13
                            opacity: 0.6
                            rotation: sessionMenu.open ? 0 : 180
                            Behavior on rotation { NumberAnimation { duration: Style.animMs } }
                        }
                    }
                    MouseArea {
                        id: sessionArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: root.sessions.length > 1 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: if (root.sessions.length > 1) sessionMenu.open = !sessionMenu.open
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.bottom: sessionPill.top
                    anchors.bottomMargin: 8
                    width: Math.max(200, list.implicitWidth + 12)
                    height: list.implicitHeight + 12
                    radius: 10
                    color: Qt.rgba(Style.menu.r, Style.menu.g, Style.menu.b, 0.92)
                    border.width: 1
                    border.color: Style.veil(0.14)
                    visible: opacity > 0
                    opacity: sessionMenu.open ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Style.animMs } }

                    Column {
                        id: list
                        x: 6
                        y: 6
                        width: parent.width - 12
                        Text {
                            leftPadding: 10
                            height: 24
                            verticalAlignment: Text.AlignVCenter
                            text: Style.tr("session")
                            color: Style.text3
                            font.family: Style.fontFamily
                            font.pixelSize: Style.smallFontSize
                        }
                        Repeater {
                            model: root.sessions
                            delegate: Rectangle {
                                id: row
                                required property var modelData
                                required property int index
                                width: list.width
                                implicitWidth: rowText.implicitWidth + 56
                                height: 30
                                radius: 6
                                color: rowArea.containsMouse ? Style.accent : "transparent"
                                Text {
                                    id: rowText
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: row.modelData.name
                                    color: rowArea.containsMouse ? Style.onAccent : Style.text
                                    font.family: Style.fontFamily
                                    font.pixelSize: Style.fontSize
                                }
                                Icon {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: row.index === root.sessionIndex
                                    name: "check"
                                    size: 14
                                    color: rowArea.containsMouse ? Style.onAccent : Style.text
                                }
                                MouseArea {
                                    id: rowArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        root.sessionIndex = row.index
                                        sessionMenu.open = false
                                        field.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Power (bottom right) ────────────────────────────────────
            Row {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 28
                anchors.bottomMargin: 24
                spacing: 6

                IconButton { icon: "moon"; label: Style.tr("suspend"); onClicked: root.power("suspend") }
                IconButton { icon: "rotate-ccw"; label: Style.tr("restart"); onClicked: root.power("reboot") }
                IconButton { icon: "power"; label: Style.tr("shutdown"); onClicked: root.power("poweroff") }
            }
        }
    }
}
