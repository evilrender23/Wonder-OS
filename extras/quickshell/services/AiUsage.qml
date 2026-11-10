pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io

// Quota usage of AI coding tools, read from the caches the `*-usage` scripts
// in extras/scripts write (adapted from Quickshell Rise; a systemd user timer
// runs them every 5 minutes). A provider whose cache is missing is hidden.
Singleton {
    id: root

    readonly property string cache: (Quickshell.env("XDG_CACHE_HOME") || Quickshell.env("HOME") + "/.cache")
    property var providers: []      // { id, name, icon, windows: [{ label, percent, resetsAt }], updated, error }
    readonly property int maxPercent: providers.reduce((m, p) => Math.max(m, ...p.windows.map(w => w.percent || 0)), 0)

    readonly property var specs: [
        { id: "claude", name: "Claude Code", file: "claude-usage.json", script: "claude-usage" },
        { id: "codex", name: "Codex", file: "codex-usage.json", script: "codex-usage" },
        { id: "opencode", name: "OpenCode", file: "opencode-usage.json", script: "opencode-usage" }
    ]

    function refreshNow() {
        Quickshell.execDetached(["sh", "-c", "for s in claude-usage codex-usage opencode-usage; do command -v $s >/dev/null && $s >/dev/null 2>&1 & done; wait"])
        reloadTimer.restart()
    }

    function resetIn(ts) {
        const s = Number(ts) - Date.now() / 1000
        if (!(Number(ts) > 0) || s <= 0) return ""
        const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60)
        return h >= 24 ? `${Math.floor(h / 24)}d ${h % 24}h` : h > 0 ? `${h}h ${m}m` : `${m}m`
    }

    // All three scripts write "5h-utilization"/"7d-utilization" (0–1, as
    // strings) with unix-second resets; Codex adds a `windows` list.
    function normalise(spec, j) {
        const out = { id: spec.id, name: spec.name, windows: [], updated: j.updated_at || "",
            stale: j._source === "stale", status: j.status || "", plan: j._plan || "",
            today: Number(j._today_tokens) || 0, perHour: Number(j._rate_per_hour) || 0 }
        const add = (label, util, reset) => {
            if (util === undefined || util === null || util === "" || isNaN(Number(util))) return
            out.windows.push({ label: label, percent: Math.round(Number(util) * 100), resetsIn: resetIn(reset) })
        }
        if (Array.isArray(j.windows) && j.windows.length > 0) {
            for (const w of j.windows) add(w.label || `${w.minutes} min`, w.utilization, w.reset)
        } else {
            add("5 hours", j["5h-utilization"], j["5h-reset"])
            add("7 days", j["7d-utilization"], j["7d-reset"])
        }
        return out
    }

    Instantiator {
        model: root.specs
        delegate: FileView {
            required property var modelData
            path: `${root.cache}/${modelData.file}`
            watchChanges: true
            printErrors: false
            onFileChanged: reload()
            onLoaded: {
                let parsed = null
                try { parsed = root.normalise(modelData, JSON.parse(text())) } catch (e) {}
                const others = root.providers.filter(p => p.id !== modelData.id)
                root.providers = (parsed ? others.concat([parsed]) : others)
                    .sort((a, b) => root.specs.findIndex(s => s.id === a.id) - root.specs.findIndex(s => s.id === b.id))
            }
        }
    }

    Timer { id: reloadTimer; interval: 20000 }
}
