pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// Pending updates. With WonderUpdate (apps/wonderupdate) installed it does the
// checking — repositories, AUR, Flatpak and the Wonderland desktop — and this
// follows its status file, so its timer's checks and a finished upgrade show up
// here too. Without it: repo updates from `checkupdates` and AUR updates from
// the installed helper, applied by running the helper in a terminal.
Singleton {
    id: root

    property var repo: []        // { name, from, to }
    property var aur: []
    property var flatpak: []
    property var wonderland: []
    property var commits: []     // what is new in the Wonderland checkout
    readonly property int count: repo.length + aur.length + flatpak.length + wonderland.length
    readonly property var sections: [
        { title: "Repositories", items: repo },
        { title: "AUR", items: aur },
        { title: "Flatpak", items: flatpak },
        { title: "Wonderland", items: wonderland }
    ].filter(s => s.items.length > 0)
    property bool wonderUpdate: false
    property bool checking: false
    property string helper: ""
    property string lastChecked: ""
    property string error: ""

    function check() {
        if (checking) return
        checking = true
        error = ""
        probe.running = true
    }

    function parse(text) {
        return text.trim().split("\n").filter(l => l.includes(" -> ")).map(l => {
            const m = l.match(/^(\S+)\s+(\S+)\s+->\s+(\S+)/)
            return m ? { name: m[1], from: m[2], to: m[3] } : null
        }).filter(Boolean)
    }

    function load(json) {
        let status
        try {
            status = JSON.parse(json)
        } catch (e) {
            // Caught half written; the next change notification brings the rest.
            return
        }
        if (status.checked === undefined) return
        repo = status.repo || []
        aur = status.aur || []
        flatpak = status.flatpak || []
        wonderland = status.wonderland || []
        commits = status.commits || []
        helper = status.helper || ""
        error = (status.errors || []).length > 0 ? `Could not check: ${status.errors.join(", ")}` : ""
        lastChecked = Qt.formatDateTime(new Date(status.checked * 1000), "HH:mm")
    }

    function apply() {
        if (wonderUpdate) {
            Quickshell.execDetached(["wonderupdate", "--terminal"])
            return
        }
        const cmd = helper ? `${helper} -Syu` : "sudo pacman -Syu"
        Quickshell.execDetached(["sh", "-c", `\${TERMINAL:-wonderterm} -e sh -c '${cmd}; echo; echo "Done — press Enter to close"; read _'`])
        after.restart()
    }

    Process {
        id: probe
        command: ["sh", "-c", "if command -v wonderupdate >/dev/null; then echo wonderupdate; echo @@; wonderupdate --check --json; exit; fi; for h in paru yay; do command -v $h >/dev/null && { echo $h; break; }; done; echo @@; checkupdates 2>/dev/null; echo @@; for h in paru yay; do command -v $h >/dev/null && { $h -Qua 2>/dev/null; break; }; done"]
        stdout: StdioCollector {
            onStreamFinished: {
                const [helper, repo, aur] = text.split(/^@@$/m)
                root.wonderUpdate = (helper || "").trim() === "wonderupdate"
                if (root.wonderUpdate) {
                    root.load(repo || "")
                    return
                }
                root.helper = (helper || "").trim()
                root.repo = root.parse(repo || "")
                root.aur = root.parse(aur || "")
                root.lastChecked = Qt.formatDateTime(new Date(), "HH:mm")
            }
        }
        onExited: code => {
            root.checking = false
            if (code !== 0) root.error = "Check failed"
        }
    }

    FileView {
        path: `${Quickshell.env("XDG_STATE_HOME") || Quickshell.env("HOME") + "/.local/state"}/wonderupdate/status.json`
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.load(text())
    }

    // Re-check a while after an upgrade terminal opens, and hourly.
    Timer { id: after; interval: 120000; onTriggered: root.check() }
    Timer { interval: 3600000; running: true; repeat: true; triggeredOnStart: true; onTriggered: root.check() }
}
