pragma Singleton
import QtQuick
import Quickshell

// Small session-wide toggles that windows bind to (the idle inhibitor needs a
// window, so each bar carries one bound here) and the power actions.
Singleton {
    id: root

    property bool idleInhibited: false

    function lock() { Quickshell.execDetached(["sh", "-c", "loginctl lock-session"]) }
    function logout() { Compositor.action("quit") }
    function suspend() { Quickshell.execDetached(["systemctl", "suspend"]) }
    function reboot() { Quickshell.execDetached(["systemctl", "reboot"]) }
    function poweroff() { Quickshell.execDetached(["systemctl", "poweroff"]) }
    function terminal(cmd) {
        Quickshell.execDetached(["sh", "-c", cmd ? `\${TERMINAL:-wonderterm} -e ${cmd}` : "${TERMINAL:-wonderterm}"])
    }
}
