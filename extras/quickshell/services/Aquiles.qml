pragma Singleton
import QtQuick
import QtWebSockets
import Quickshell
import Quickshell.Io

// AquilesAI: the shell's own agent. One transcript, four engines:
//
//   hermes  Hermes Agent's canonical gateway, the same JSON-RPC-over-WebSocket
//           contract Hermes Desktop speaks (sessions, streaming, tools,
//           approvals, slash commands, every provider and its API key).
//   claude  Claude Code CLI      `claude -p … --output-format stream-json`
//   codex   Codex CLI (ChatGPT)  `codex exec --json …`
//   agy     Antigravity CLI      `agy -p … --output-format stream-json`
//
// The CLIs run headless, one process per turn, resumed by their own session
// id; their sign-in (subscription or key) is whatever the CLI already has.
Singleton {
    id: root

    readonly property string home: Quickshell.env("HOME")
    readonly property var engines: [
        { id: "hermes", name: "Hermes Agent", detail: I18n.tr("gateway local: todos los proveedores y API keys") },
        { id: "claude", name: "Claude Code", detail: I18n.tr("CLI claude") },
        { id: "codex", name: "Codex (ChatGPT)", detail: I18n.tr("CLI codex") },
        { id: "agy", name: "Antigravity", detail: I18n.tr("CLI agy") }
    ]
    readonly property string backend: Settings.data.aquilesBackend
    readonly property bool isHermes: backend === "hermes"
    readonly property string cwd: Settings.data.aquilesCwd || home
    function engineName(id) { const e = engines.find(e => e.id === id); return e ? e.name : id }

    // ── transcript ──────────────────────────────────────────────────────
    // Rows: kind user | assistant | tool | system | error | approval | clarify | secret.
    property ListModel messages: ListModel {}
    property bool busy: false
    property string status: ""          // what the agent is doing right now
    property string title: ""
    property string usageText: ""
    property int cur: -1                // the assistant row being streamed
    signal turnFinished()

    function row(kind, text, extra) {
        messages.append(Object.assign({ kind: kind, text: text || "", reasoning: "", name: "", detail: "",
            result: "", state: "", pid: "", choices: "[]" }, extra || {}))
        return messages.count - 1
    }
    function note(text) { cur = -1; row("system", text) }
    function fail(text) { cur = -1; row("error", String(text || "error")) }
    function appendText(delta) {
        if (!delta) return
        if (cur < 0) cur = row("assistant", "")
        messages.setProperty(cur, "text", messages.get(cur).text + delta)
    }
    function appendReasoning(delta) {
        if (!delta) return
        if (cur < 0) cur = row("assistant", "")
        messages.setProperty(cur, "reasoning", messages.get(cur).reasoning + delta)
    }
    function clip(s, n) {
        s = typeof s === "string" ? s : JSON.stringify(s)
        return s && s.length > n ? s.slice(0, n) + "\n… " + I18n.tr("(%1 caracteres más)").arg(String(s.length - n)) : (s || "")
    }
    function toolStart(pid, name, detail) {
        cur = -1
        return row("tool", "", { pid: String(pid || ""), name: name || "tool", detail: clip(detail, 600), state: "running" })
    }
    function toolEnd(pid, ok, result, name, detail) {
        pid = String(pid || "")
        for (let i = messages.count - 1; i >= 0; i--) {
            const m = messages.get(i)
            if (m.kind === "tool" && m.pid === pid && m.state === "running") {
                messages.setProperty(i, "state", ok ? "ok" : "error")
                messages.setProperty(i, "result", clip(result, 4000))
                return
            }
        }
        row("tool", "", { pid: pid, name: name || "tool", detail: clip(detail, 600), state: ok ? "ok" : "error", result: clip(result, 4000) })
        cur = -1
    }
    function settlePrompts(pid) {
        for (let i = 0; i < messages.count; i++) {
            const m = messages.get(i)
            if ((m.kind === "approval" || m.kind === "clarify" || m.kind === "secret") && m.state === "pending" && (!pid || m.pid === pid))
                messages.setProperty(i, "state", "done")
        }
    }
    function uuid() {
        return "aq-" + Date.now().toString(36) + "-" + Math.random().toString(36).slice(2, 12)
    }

    // ── public actions ──────────────────────────────────────────────────
    function setBackend(id) {
        if (id === backend || busy) return
        newChat()
        Settings.data.aquilesBackend = id
    }

    function newChat() {
        if (busy) stop()
        if (isHermes && sid && subscription) rpc("session.detach", { session_id: sid, subscription_id: subscription })
        messages.clear()
        cur = -1; sid = ""; subscription = ""; title = ""; usageText = ""; status = ""; asks = ({})
        chatId = ""; nativeId = ""
        info = ({})
    }

    function send(text) {
        text = String(text || "").trim()
        if (!text) return
        if (text === "/new" || text === "/clear" || text === "/reset") { newChat(); return }
        if (isHermes) hermesSend(text)
        else cliSend(text)
    }

    function stop() {
        if (isHermes) {
            if (sid) rpc("session.interrupt", { session_id: sid, execution_generation: gen }, (r, e) => {
                if (e && e.data && e.data.execution_generation !== undefined) {
                    gen = e.data.execution_generation
                    rpc("session.interrupt", { session_id: sid, execution_generation: gen })
                }
            })
        } else if (proc.running) {
            stopped = true
            proc.running = false
        }
    }

    // ══ Hermes gateway ═════════════════════════════════════════════════
    property string link: "off"         // off | starting | connecting | open | error
    property string linkError: ""
    property string sid: ""
    property string subscription: ""
    property int revision: 0
    property int gen: 0
    property var info: ({})             // the live session's model, provider, cwd, effort…
    property var sessions: []
    property var providers: []
    property string defaultModel: ""
    property string defaultProvider: ""
    property var commands: []           // [[ "/name", "description" ], …]
    property int seq: 0
    property var pending: ({})
    property var queue: []              // calls made before the socket opened
    property int retries: 0

    // The live session's model, else the one picked for new chats, else Hermes' own.
    readonly property var models: Settings.data.aquilesModels || ({})
    readonly property var picked: models.hermes || ({})
    readonly property string model: String((isHermes ? (info.model || picked.model || defaultModel) : models[backend]) || "")
    readonly property string provider: isHermes
        ? (info.provider || (picked.model ? picked.provider : "") || defaultProvider)
        : engineName(backend)

    function connect() {
        if (link === "starting" || link === "connecting" || link === "open") return
        link = "starting"
        linkError = ""
        boot.running = true
    }

    function rpc(method, params, cb) {
        if (ws.status !== WebSocket.Open) {
            queue.push([method, params, cb])
            connect()
            return
        }
        const id = ++seq
        if (cb) pending[id] = cb
        ws.sendTextMessage(JSON.stringify({ jsonrpc: "2.0", id: id, method: method, params: params || {} }))
    }

    // The bootstrap is the one Hermes' own TUI uses: it starts (or finds) the
    // profile's gateway and prints a one-use WebSocket grant.
    Process {
        id: boot
        command: ["sh", "-c", "R=${HERMES_AGENT_ROOT:-$HOME/.hermes/hermes-agent}; "
            + "PY=${HERMES_PYTHON:-$(ls -d $HOME/.hermes/tools/python-*/bin/python3 2>/dev/null | head -1)}; "
            + "[ -x \"$PY\" ] || PY=python3; [ -d \"$R\" ] || { echo 'Hermes Agent no está instalado en '$R >&2; exit 3; }; "
            + "cd \"$R\" && PYTHONPATH=\"$R\" HERMES_PYTHON_SRC_ROOT=\"$R\" \"$PY\" ui-tui/scripts/gateway_bootstrap.py --start"]
        stdout: StdioCollector {
            onStreamFinished: {
                let grant = null
                try { grant = JSON.parse(text) } catch (e) {}
                if (!grant || !grant.url) return
                root.link = "connecting"
                ws.active = false
                ws.requestedSubprotocols = grant.protocols || []
                ws.url = grant.url
                ws.active = true
            }
        }
        stderr: StdioCollector { id: bootErr }
        onExited: code => {
            if (code !== 0) {
                root.link = "error"
                root.linkError = bootErr.text.trim().split("\n").slice(-3).join("\n") || I18n.tr("gateway_bootstrap salió con %1").arg(String(code))
                root.dropQueue()
            }
        }
    }

    function dropQueue() {
        const q = queue
        queue = []
        for (const c of q) if (c[2]) c[2](null, { message: linkError || I18n.tr("sin conexión con Hermes") })
    }

    WebSocket {
        id: ws
        active: false
        onStatusChanged: status => {
            if (status === WebSocket.Open) {
                root.link = "open"
                root.retries = 0
                // Without this the gateway gives the agent no way to ask:
                // approvals, clarify and password prompts are requests to us.
                root.rpc("client.capabilities", { server_requests: true })
                const q = root.queue
                root.queue = []
                for (const c of q) root.rpc(c[0], c[1], c[2])
                root.refreshSessions()
                root.refreshProviders(false)
                root.rpc("commands.catalog", {}, r => { if (r && r.pairs) root.commands = r.pairs })
                // A dropped socket loses the subscription, not the session.
                if (root.sid) root.openSession(root.sid, true)
            } else if (status === WebSocket.Closed || status === WebSocket.Error) {
                const was = root.link
                if (status === WebSocket.Error) root.linkError = errorString
                root.link = status === WebSocket.Error ? "error" : "off"
                const p = root.pending
                root.pending = ({})
                for (const id in p) p[id](null, { message: I18n.tr("conexión cerrada") })
                if (root.busy && root.isHermes) { root.busy = false; root.status = "" }
                active = false
                if (was === "open" && root.retries < 5) { root.retries++; reconnect.restart() }
                else root.dropQueue()
            }
        }
        onTextMessageReceived: message => {
            let f
            try { f = JSON.parse(message) } catch (e) { return }
            if (f.method === "event") { root.onEvent(f.params || {}); return }
            if (f.method) {
                if (f.id !== undefined) root.onRequest(f.id, f.method, f.params || {})
                return
            }
            const cb = root.pending[f.id]
            if (!cb) return
            delete root.pending[f.id]
            cb(f.error ? null : (f.result || {}), f.error || null)
        }
    }
    Timer { id: reconnect; interval: 1500; onTriggered: root.connect() }

    function refreshSessions() {
        rpc("session.list", { limit: 80 }, r => { if (r) sessions = r.sessions || [] })
    }
    function refreshProviders(force) {
        const params = { include_unconfigured: true }
        if (force) params.refresh = true
        if (sid) params.session_id = sid
        rpc("model.options", params, r => {
            if (!r) return
            providers = (r.providers || []).slice().sort((a, b) => (b.authenticated ? 1 : 0) - (a.authenticated ? 1 : 0))
            defaultModel = r.model || ""
            defaultProvider = r.provider || ""
        })
    }

    function adopt(snap) {
        sid = snap.session_id
        subscription = snap.subscription_id || ""
        revision = snap.revision || 0
        gen = snap.execution_generation || 0
        info = snap.info || ({})
        title = info.title || ""
        showUsage(info.usage)
        busy = !!snap.running
    }

    function ensureSession(then) {
        if (sid) { then(); return }
        const p = { source: "gui", cwd: cwd, request_id: uuid() }
        if (picked.model) {
            p.model = picked.model
            if (picked.provider) p.provider = picked.provider
        }
        rpc("session.create", p, (r, e) => {
            if (e) { fail(I18n.tr("No se pudo crear la sesión: %1").arg(e.message)); return }
            adopt(r)
            then()
        })
    }

    function openSession(id, quiet) {
        if (busy && !quiet) return
        rpc("session.resume", { session_id: id }, (r, e) => {
            if (e) { if (!quiet) fail(I18n.tr("No se pudo abrir la sesión: %1").arg(e.message)); return }
            if (subscription && sid && sid !== r.session_id) rpc("session.detach", { session_id: sid, subscription_id: subscription })
            messages.clear()
            cur = -1
            adopt(r)
            for (const m of (r.messages || [])) {
                if (m.role === "user") row("user", m.text || "")
                else if (m.role === "assistant") { if (m.text || m.reasoning) row("assistant", m.text || "", { reasoning: m.reasoning || "" }) }
                else if (m.role === "tool") row("tool", "", { name: m.name || "tool", detail: clip(m.context || "", 600), result: clip(m.text || "", 4000), state: "ok" })
            }
            cur = -1
            for (const q of (r.open_requests || [])) onRequest(q.id, q.method, q.params || {})
        })
    }

    function hermesSend(text) {
        if (text[0] === "/") { ensureSession(() => slash(text)); return }
        row("user", text)
        cur = -1
        if (!title) title = text.slice(0, 60)
        ensureSession(() => {
            const call = busy ? "session.steer" : "prompt.submit"
            const params = busy ? { session_id: sid, execution_generation: gen, text: text }
                : { session_id: sid, text: text, input_id: uuid() }
            if (!busy) { busy = true; status = I18n.tr("enviando…") }
            rpc(call, params, (r, e) => {
                if (e) { fail(e.message); if (call === "prompt.submit") { busy = false; status = "" } }
            })
        })
    }

    // Slash commands run where Hermes runs them: the gateway executes what it
    // owns and hands back the prompt for the ones that are really a message.
    function slash(text) {
        row("user", text)
        cur = -1
        const done = (r, e) => {
            if (e) { fail(`${text.split(" ")[0]}: ${e.message}`); return }
            if (r.type === "send" && r.message) { submitRaw(r.message); return }
            const out = r.output || r.message || r.notice || r.display || r.warning || ""
            if (out) note(out)
            refreshSessions()
        }
        rpc("slash.exec", { session_id: sid, command: text }, (r, e) => {
            if (!e) { done(r, null); return }
            const m = text.slice(1).split(/\s+/)
            rpc("command.dispatch", { session_id: sid, name: m[0], arg: m.slice(1).join(" ") }, done)
        })
    }
    function submitRaw(text) {
        busy = true
        status = I18n.tr("enviando…")
        rpc("prompt.submit", { session_id: sid, text: text, input_id: uuid() }, (r, e) => {
            if (e) { fail(e.message); busy = false; status = "" }
        })
    }
    function complete(text, cb) {
        if (!isHermes || link !== "open") {
            cb([])
            return
        }
        rpc("complete.slash", { text: text }, r => cb(r && r.items ? r.items : []))
    }

    // Revision-fenced edits (rename, model, effort, delete…); a stale fence
    // answers with the current one, so one retry lands it.
    function mutate(target, op, payload, cb, retried) {
        const fenced = op !== "rename" && op !== "archive"
        const go = (rev, g) => {
            const params = { session_id: target, request_id: uuid(), expected_revision: rev, operation: op, payload: payload }
            if (fenced) params.expected_generation = g
            rpc("session.mutate", params, (r, e) => {
                if (e && !retried && e.data && e.data.revision !== undefined) {
                    if (target === sid) { revision = e.data.revision; gen = e.data.execution_generation }
                    mutateRetry(target, op, payload, cb, e.data.revision, e.data.execution_generation)
                    return
                }
                if (r && target === sid) {
                    if (r.revision !== undefined && r.revision !== null) revision = r.revision
                    if (r.execution_generation !== undefined) gen = r.execution_generation
                }
                if (cb) cb(r, e)
            })
        }
        if (target === sid) go(revision, gen)
        else rpc("session.resume", { session_id: target, omit_messages: true }, (r, e) => {
            if (e) { if (cb) cb(null, e); return }
            if (r.subscription_id) rpc("session.detach", { session_id: target, subscription_id: r.subscription_id })
            go(r.revision, r.execution_generation)
        })
    }
    function mutateRetry(target, op, payload, cb, rev, g) {
        const params = { session_id: target, request_id: uuid(), expected_revision: rev, operation: op, payload: payload }
        if (op !== "rename" && op !== "archive") params.expected_generation = g
        rpc("session.mutate", params, (r, e) => {
            if (r && target === sid) {
                if (r.revision !== undefined && r.revision !== null) revision = r.revision
                if (r.execution_generation !== undefined) gen = r.execution_generation
            }
            if (cb) cb(r, e)
        })
    }

    // ── model manager ───────────────────────────────────────────────────
    // One picker for every engine: Hermes lists each connected provider's
    // models; a CLI lists its aliases plus whatever it reports itself, and
    // takes any other name typed by hand. The choice sticks for new chats.
    property var cliModels: ({
        claude: [{ id: "haiku", name: "Haiku", detail: I18n.tr("el más rápido y barato") },
                 { id: "sonnet", name: "Sonnet", detail: I18n.tr("equilibrado") },
                 { id: "opus", name: "Opus", detail: I18n.tr("el más capaz") },
                 { id: "fable", name: "Fable", detail: "" }],
        codex: [], agy: []
    })

    // -> [{ id, provider, group, name, detail, current }]; every word of the query must match.
    function modelChoices(query) {
        const q = String(query || "").trim()
        const words = q.toLowerCase().split(/\s+/).filter(Boolean)
        const hit = s => words.every(w => s.toLowerCase().includes(w))
        const out = []
        if (isHermes) {
            for (const p of providers) {
                if (!p.authenticated) continue
                const first = p.featured_models || []
                for (const m of first.concat((p.models || []).filter(m => !first.includes(m))))
                    if (hit(m + " " + p.name + " " + p.slug))
                        out.push({ id: m, provider: p.slug, group: p.name, name: m, detail: "", current: m === model && p.slug === provider })
            }
            return out
        }
        const group = engineName(backend)
        if (!q) out.push({ id: "", provider: "", group: group, name: I18n.tr("Por defecto"), detail: I18n.tr("el que tenga configurado la CLI"), current: model === "" })
        const known = cliModels[backend] || []
        for (const m of known)
            if (hit(m.id + " " + m.name))
                out.push({ id: m.id, provider: "", group: group, name: m.name || m.id, detail: m.name && m.name !== m.id ? [m.id, m.detail].filter(Boolean).join(" · ") : (m.detail || ""), current: m.id === model })
        if (model && !known.some(m => m.id === model) && hit(model))
            out.push({ id: model, provider: "", group: group, name: model, detail: I18n.tr("escrito a mano"), current: true })
        if (q && !/\s/.test(q) && q !== model && !known.some(m => m.id === q))
            out.push({ id: q, provider: "", group: group, name: q, detail: I18n.tr("usar este nombre tal cual"), current: false })
        return out
    }

    function recentModels() {
        return (models.recent || []).filter(r => r.backend === backend && !(r.id === model && (!isHermes || r.provider === provider))
            && (!isHermes || providers.some(p => p.slug === r.provider && p.authenticated)))
            .map(r => ({ id: r.id, provider: r.provider || "", group: I18n.tr("Recientes"), name: r.id || I18n.tr("Por defecto"),
                detail: isHermes ? providerName(r.provider) : "", current: r.id === model && (!isHermes || r.provider === provider) }))
    }
    function providerName(slug) {
        const p = providers.find(p => p.slug === slug)
        return p ? p.name : slug
    }
    // The choice and the recents list go out together: the settings file
    // reloads itself on change, and two writes in a row would race that reload.
    function keepModel(model, provider) {
        const b = backend, next = Object.assign({}, models)
        if (isHermes) next.hermes = { model: model, provider: provider }
        else next[b] = model
        if (model) next.recent = [{ backend: b, id: model, provider: provider || "" }].concat((models.recent || [])
            .filter(r => !(r.backend === b && r.id === model && (r.provider || "") === (provider || "")))).slice(0, 24)
        Settings.data.aquilesModels = next
        note(isHermes ? I18n.tr("Modelo: %1").arg(model) + ` · ${providerName(provider)}`
            : model ? I18n.tr("Modelo: %1").arg(model) + ` · ${engineName(b)}` : I18n.tr("Modelo: el que %1 tenga por defecto").arg(engineName(b)))
    }

    function setModel(model, provider) {
        if (!isHermes || !sid) { keepModel(model, provider); return }   // no chat yet: it applies when one starts
        mutate(sid, "model", { model: model, provider: provider }, (r, e) => {
            if (e) { fail(I18n.tr("No se pudo cambiar el modelo: %1").arg(e.message)); return }
            info = Object.assign({}, info, { model: model, provider: provider })
            keepModel(model, provider)
        })
    }
    function setYolo(on) {
        ensureSession(() => rpc("config.set", { key: "yolo", value: on ? "on" : "off", session_id: sid }, (r, e) => {
            if (e) fail(e.message)
            else info = Object.assign({}, info, { yolo: r.value === "1" || r.value === true })
        }))
    }
    function renameSession(id, name) {
        mutate(id, "rename", { title: name }, (r, e) => {
            if (e) fail(I18n.tr("No se pudo renombrar: %1").arg(e.message))
            else if (id === sid) title = name
            refreshSessions()
        })
    }
    function deleteSession(id) {
        const own = id === sid
        mutate(id, "delete", {}, (r, e) => {
            if (e) { fail(I18n.tr("No se pudo borrar: %1").arg(e.message)); return }
            if (own) { sid = ""; subscription = ""; newChat() }
            refreshSessions()
        })
    }
    function saveKey(slug, key, cb) {
        rpc("model.save_key", { slug: slug, api_key: key }, (r, e) => {
            if (cb) cb(e ? e.message : "")
            if (!e) refreshProviders(true)
        })
    }
    function disconnectProvider(slug, cb) {
        rpc("model.disconnect", { slug: slug }, (r, e) => {
            if (cb) cb(e ? e.message : "")
            if (!e) refreshProviders(true)
        })
    }
    // ── the agent asks (server -> client requests) ──────────────────────
    // approval: a risky command. clarify: 1-5 questions, answered together.
    // sudo / secret: a masked value. Each is answered with the request's id;
    // what the shell cannot show (previews, window reads) is declined at once
    // so the tool fails fast instead of waiting out its deadline.
    property var asks: ({})             // request id -> { method, qids, answers }

    function reply(id, result, error) {
        const f = { jsonrpc: "2.0", id: id }
        if (error) f.error = error
        else f.result = result
        if (ws.status === WebSocket.Open) ws.sendTextMessage(JSON.stringify(f))
    }

    function onRequest(id, method, p) {
        if (asks[id]) return
        if (p.session_id && p.session_id !== sid) return    // another session's: it waits for its own view
        const card = (kind, text, extra) => row(kind, text, Object.assign({ pid: String(id), state: "pending" }, extra))
        cur = -1
        if (method === "approval") {
            asks[id] = { method: method }
            status = I18n.tr("esperando tu permiso")
            card("approval", p.description || I18n.tr("El agente pide permiso"), { detail: p.command || "",
                choices: JSON.stringify(p.choices && p.choices.length ? p.choices : ["once", "session", "always", "deny"]) })
        } else if (method === "clarify") {
            const qs = (p.questions || []).filter(q => q && q.qid && q.question)
            if (!qs.length) { reply(id, {}); return }
            const answers = {}
            for (const q of qs) if (p.answers && typeof p.answers[q.qid] === "string") answers[q.qid] = p.answers[q.qid]
            asks[id] = { method: method, qids: qs.map(q => q.qid), answers: answers }
            status = I18n.tr("esperando tu respuesta")
            for (const q of qs) {
                const i = card("clarify", q.question, { name: q.qid, choices: JSON.stringify(q.choices || []) })
                if (answers[q.qid] !== undefined) { messages.setProperty(i, "state", "done"); messages.setProperty(i, "detail", "→ " + answers[q.qid]) }
            }
        } else if (method === "sudo" || method === "secret") {
            asks[id] = { method: method }
            status = I18n.tr("esperando una contraseña")
            card("secret", method === "sudo" ? I18n.tr("Contraseña de sudo") : (p.prompt || p.env_var || I18n.tr("Valor secreto")),
                { detail: method === "sudo" ? (p.command || "") : (p.env_var || "") })
        } else reply(id, null, { code: -32601, message: "AquilesAI cannot answer " + method })
    }

    // choice: an approval choice, a clarify answer, or the masked value ("" = skip).
    function answer(index, choice) {
        const m = messages.get(index)
        if (!m || m.state !== "pending") return
        const id = m.pid, ask = asks[id]
        if (!ask) { messages.setProperty(index, "state", "done"); return }
        choice = String(choice || "")
        messages.setProperty(index, "state", "done")
        if (m.kind === "approval") {
            messages.setProperty(index, "result", choice)
            reply(id, { choice: choice || "deny" })
        } else if (m.kind === "secret") {
            messages.setProperty(index, "result", choice ? I18n.tr("enviado") : I18n.tr("omitido"))
            reply(id, { value: choice })
        } else {
            messages.setProperty(index, "detail", "→ " + (choice || I18n.tr("omitido")))
            ask.answers[m.name] = choice
            if (ask.qids.some(q => ask.answers[q] === undefined)) return
            reply(id, { answers: ask.answers })
        }
        delete asks[id]
        if (busy) status = I18n.tr("pensando…")
    }

    function dismissAsk(id) {
        delete asks[id]
        settlePrompts(String(id))
    }

    function showUsage(u) {
        if (!u || !u.total) return
        const k = n => n >= 1000 ? (n / 1000).toFixed(1) + "k" : String(n || 0)
        usageText = `${k(u.total)} tokens` + (u.context_percent !== undefined ? " · " + I18n.tr("contexto %1%").arg(String(u.context_percent)) : "")
    }

    function onEvent(p) {
        const t = p.type
        if (t === "sessions.changed") { sessionsDebounce.restart(); return }
        if (!sid || p.session_id !== sid) return
        const d = p.payload || {}
        if (p.execution_generation !== undefined && p.execution_generation !== null) gen = p.execution_generation
        switch (t) {
        case "message.start": busy = true; status = I18n.tr("pensando…"); cur = -1; break
        case "message.delta": status = ""; appendText(d.text); break
        case "reasoning.delta": status = I18n.tr("razonando…"); appendReasoning(d.text); break
        case "thinking.delta": if (d.text) status = d.text; break
        case "tool.generating": status = I18n.tr("preparando %1…").arg(d.name || I18n.tr("herramienta")); break
        case "tool.start": status = `${d.name || "tool"}…`; toolStart(d.tool_id, d.name, d.context || (d.args ? JSON.stringify(d.args) : "")); break
        case "tool.complete": toolEnd(d.tool_id, !d.is_error, d.result, d.name, d.context); break
        case "request.cancel": dismissAsk(d.id); break
        case "session.usage": showUsage(d.usage); break
        case "session.info":
            if (d.revision !== undefined) revision = d.revision
            if (d.model || d.provider || d.title) {
                info = Object.assign({}, info, d)
                if (d.title) title = d.title
            }
            break
        case "message.complete":
            if (d.text && (cur < 0 || !messages.get(cur).text)) {
                if (cur < 0) cur = row("assistant", "")
                messages.setProperty(cur, "text", d.text)
            }
            if (d.status && d.status !== "complete") note(I18n.tr("Turno %1").arg(d.outcome || d.status))
            showUsage(d.usage)
            busy = false; status = ""; cur = -1
            settlePrompts("")
            asks = ({})
            turnFinished()
            break
        case "error": fail(d.message || d.error || I18n.tr("error del gateway")); break
        case "session.replay_gap": openSession(sid, true); break
        }
    }
    Timer { id: sessionsDebounce; interval: 400; onTriggered: root.refreshSessions() }

    // ══ CLI engines ═════════════════════════════════════════════════════
    property var bins: ({})             // engine id -> absolute path, when installed
    property bool binsReady: false
    property string chatId: ""          // our id for the CLI chat on screen
    property string nativeId: ""        // the CLI's own session / thread / conversation id
    property var chats: []              // stored CLI chats (all engines)
    property bool stopped: false
    property string errTail: ""
    property int agyStep: -1

    readonly property string binPath: [home + "/.local/bin", home + "/.npm-global/bin", home + "/.bun/bin",
        home + "/.cargo/bin", home + "/.antigravity/bin", "/usr/local/bin"].join(":")

    function probeBins() { probe.running = true }
    Process {
        id: probe
        running: true
        command: ["sh", "-c", `PATH="$PATH:${root.binPath}"; for b in claude codex agy; do p=$(command -v $b 2>/dev/null) && echo "$b=$p"; done
            command -v agy >/dev/null 2>&1 && timeout 8 agy models 2>/dev/null | sed 's/^/agy:/'
            [ -f "$HOME/.codex/models_cache.json" ] && grep -o '"slug": *"[^"]*"' "$HOME/.codex/models_cache.json" | sed 's/"$//; s/.*"/codex:/'
            true`]
        stdout: StdioCollector {
            onStreamFinished: {
                const found = {}, listed = { codex: [], agy: [] }
                for (const line of text.split("\n")) {
                    const m = /^(agy|codex):(\S+)\s*(.*)$/.exec(line)      // "<slug> <display name>"
                    if (m) { if (!listed[m[1]].some(x => x.id === m[2])) listed[m[1]].push({ id: m[2], name: m[3] || m[2], detail: "" }); continue }
                    const i = line.indexOf("=")
                    if (i > 0) found[line.slice(0, i)] = line.slice(i + 1)
                }
                root.bins = found
                root.cliModels = Object.assign({}, root.cliModels, listed)
                root.binsReady = true
            }
        }
    }

    // read: answers and reads only · edits: may edit files · full: no prompts.
    readonly property string permission: Settings.data.aquilesPermission

    function cliCommand(text) {
        const b = backend, bin = bins[b], m = models[b] || "", perm = permission
        let c = []
        if (b === "claude") {
            c = [bin, "-p", text, "--output-format", "stream-json", "--verbose", "--include-partial-messages",
                "--permission-mode", perm === "full" ? "bypassPermissions" : perm === "edits" ? "acceptEdits" : "plan"]
            if (m) c.push("--model", m)
            if (nativeId) c.push("--resume", nativeId)
        } else if (b === "codex") {
            c = [bin, "exec", "--json", "--skip-git-repo-check", "-c",
                `sandbox_mode="${perm === "full" ? "danger-full-access" : perm === "edits" ? "workspace-write" : "read-only"}"`]
            if (m) c.push("-m", m)
            if (nativeId) c.push("resume", nativeId)
            c.push(text)
        } else {
            c = [bin, "-p", text, "--output-format", "stream-json"]
            if (perm === "full") c.push("--dangerously-skip-permissions")
            if (m) c.push("--model", m)
            if (nativeId) c.push("--conversation", nativeId)
        }
        return c
    }

    function cliSend(text) {
        if (busy) { note(I18n.tr("Espera a que termine el turno (o deténlo) para enviar otro mensaje.")); return }
        if (!bins[backend]) {
            fail(I18n.tr("%1: no encuentro el ejecutable «%2» en el PATH. Instálalo e inicia sesión en una terminal, o elige otro motor.").arg(engineName(backend)).arg(backend))
            return
        }
        row("user", text)
        cur = -1
        if (!chatId) { chatId = uuid(); title = text.slice(0, 60) }
        busy = true
        stopped = false
        errTail = ""
        agyStep = -1
        status = I18n.tr("pensando…")
        // Quickshell hands the child an open stdin pipe that never reaches EOF, and
        // `codex exec` waits on it for extra prompt text before starting the turn.
        proc.command = ["sh", "-c", 'exec "$0" "$@" </dev/null'].concat(cliCommand(text))
        proc.workingDirectory = cwd
        proc.running = true
    }

    Process {
        id: proc
        stdout: SplitParser {
            onRead: line => {
                if (!line.trim()) return
                let j
                try { j = JSON.parse(line) } catch (e) { root.errTail = (root.errTail + "\n" + line).slice(-1200); return }
                if (root.backend === "claude") root.onClaude(j)
                else if (root.backend === "codex") root.onCodex(j)
                else root.onAgy(j)
            }
        }
        stderr: SplitParser { onRead: line => root.errTail = (root.errTail + "\n" + line).slice(-1200) }
        onExited: code => {
            if (root.stopped) root.note(I18n.tr("Detenido."))
            else if (code !== 0) root.fail(I18n.tr("%1 salió con código %2").arg(root.engineName(root.backend)).arg(String(code)) + (root.errTail.trim() ? "\n" + root.errTail.trim() : ""))
            for (let i = 0; i < root.messages.count; i++)
                if (root.messages.get(i).kind === "tool" && root.messages.get(i).state === "running")
                    root.messages.setProperty(i, "state", "error")
            root.busy = false
            root.status = ""
            root.cur = -1
            root.saveChat()
            root.turnFinished()
        }
    }

    function brief(input) {
        if (!input || typeof input !== "object") return String(input || "")
        return input.command || input.CommandLine || input.file_path || input.path || input.pattern
            || input.query || input.url || input.description || JSON.stringify(input)
    }
    function flat(content) {
        if (typeof content === "string") return content
        if (Array.isArray(content)) return content.map(c => c.text || (c.type ? `[${c.type}]` : "")).join("\n")
        return content ? JSON.stringify(content) : ""
    }

    function onClaude(j) {
        if (j.type === "system" && j.subtype === "init") { nativeId = j.session_id || nativeId; return }
        if (j.type === "stream_event") {
            const e = j.event || {}, d = e.delta || {}
            if (e.type === "content_block_delta") {
                if (d.type === "text_delta") { status = ""; appendText(d.text) }
                else if (d.type === "thinking_delta") { status = I18n.tr("razonando…"); appendReasoning(d.thinking) }
            } else if (e.type === "content_block_start" && e.content_block && e.content_block.type === "tool_use")
                status = I18n.tr("preparando %1…").arg(e.content_block.name)
            return
        }
        if (j.type === "assistant" && j.message) {
            for (const b of (j.message.content || []))
                if (b.type === "tool_use") { status = `${b.name}…`; toolStart(b.id, b.name, brief(b.input)) }
            return
        }
        if (j.type === "user" && j.message && Array.isArray(j.message.content)) {
            for (const b of j.message.content)
                if (b.type === "tool_result") toolEnd(b.tool_use_id, !b.is_error, flat(b.content))
            status = I18n.tr("pensando…")
            return
        }
        if (j.type === "result") {
            nativeId = j.session_id || nativeId
            if (j.is_error) fail(j.result || j.subtype || "error")
            else if (j.result && (cur < 0 || !messages.get(cur).text)) { cur = -1; appendText(j.result) }
            const u = j.usage || {}
            usageText = `${(u.input_tokens || 0) + (u.cache_read_input_tokens || 0) + (u.cache_creation_input_tokens || 0)} in · ${u.output_tokens || 0} out`
                + (j.total_cost_usd ? ` · $${j.total_cost_usd.toFixed(4)}` : "")
        }
    }

    function onCodex(j) {
        const it = j.item || {}
        switch (j.type) {
        case "thread.started": nativeId = j.thread_id || nativeId; break
        case "turn.started": status = I18n.tr("pensando…"); break
        case "item.started":
            if (it.type === "command_execution") { status = "shell…"; toolStart(it.id, "shell", it.command) }
            else if (it.type === "mcp_tool_call") toolStart(it.id, `${it.server || "mcp"}.${it.tool || "tool"}`, brief(it.arguments))
            else if (it.type === "web_search") toolStart(it.id, "web_search", it.query || "")
            break
        case "item.updated":
        case "item.completed":
            if (j.type === "item.updated" && it.type !== "agent_message") break
            if (it.type === "agent_message") { if (j.type === "item.completed") { cur = -1; appendText(it.text); cur = -1 } }
            else if (it.type === "reasoning") { cur = -1; appendReasoning(it.text) }
            else if (it.type === "command_execution") toolEnd(it.id, it.exit_code === 0 || it.status === "completed", it.aggregated_output || "", "shell", it.command)
            else if (it.type === "file_change") toolEnd(it.id, it.status !== "failed", "", "edit", (it.changes || []).map(c => `${c.kind || "update"} ${c.path}`).join("\n"))
            else if (it.type === "mcp_tool_call") toolEnd(it.id, it.status !== "failed", flat(it.result && it.result.content) || (it.error && it.error.message) || "", it.tool)
            else if (it.type === "web_search") toolEnd(it.id, true, "", "web_search", it.query || "")
            else if (it.type === "error") fail(it.message)
            break
        case "turn.completed": {
            const u = j.usage || {}
            usageText = `${(u.input_tokens || 0) + (u.cached_input_tokens || 0)} in · ${u.output_tokens || 0} out`
            break
        }
        case "turn.failed": fail((j.error && j.error.message) || I18n.tr("el turno falló")); break
        case "error": fail(j.message || I18n.tr("error de codex")); break
        }
    }

    function onAgy(j) {
        if (j.event === "init") { nativeId = j.conversation_id || nativeId; return }
        if (j.event === "step_update") {
            const s = j.step_update || {}
            if (s.conversation_id) nativeId = s.conversation_id
            if (s.step_type === "agent_response") {
                if (s.step_index !== agyStep) { agyStep = s.step_index; cur = -1 }
                status = ""
                appendText(s.text_delta)
            } else if (s.step_type === "tool") {
                const ti = s.tool_info || {}, pid = "step" + s.step_index, name = s.tool_name || ti.name || "tool"
                if (s.state === "DONE") toolEnd(pid, !ti.error, ti.error ? (ti.error.message || "error") : (ti.output || ""), name, brief(ti.parameters))
                else {
                    let seen = false
                    for (let i = messages.count - 1; i >= 0 && !seen; i--) seen = messages.get(i).pid === pid
                    if (!seen) toolStart(pid, name, brief(ti.parameters))
                    status = `${name}…`
                }
            } else if (s.step_type === "checkpoint" && s.usage)
                usageText = `${s.usage.input_tokens || 0} in · ${s.usage.output_tokens || 0} out`
            return
        }
        if (j.event === "result") {
            const r = j.result || {}
            if (r.conversation_id) nativeId = r.conversation_id
            if (r.status && r.status !== "SUCCESS") fail(r.error || `agy: ${r.status}`)
            else if (r.response && (cur < 0 || !messages.get(cur).text)) { cur = -1; appendText(r.response) }
            if (r.usage) usageText = `${r.usage.input_tokens || 0} in · ${r.usage.output_tokens || 0} out`
        }
    }

    // CLI chats are remembered here (the CLIs keep the context; this keeps
    // what was on screen and the id to resume with).
    FileView {
        id: store
        path: Settings.stateDir + "/aquiles.json"
        printErrors: false
        atomicWrites: true
        onLoaded: { try { root.chats = JSON.parse(text()).chats || [] } catch (e) { root.chats = [] } }
    }
    function saveChat() {
        if (isHermes || !chatId) return
        const rows = []
        for (let i = Math.max(0, messages.count - 200); i < messages.count; i++) {
            const m = messages.get(i)
            rows.push({ kind: m.kind, text: m.text, reasoning: m.reasoning, name: m.name, detail: m.detail,
                result: clip(m.result, 1200), state: m.state })
        }
        const rest = chats.filter(c => c.id !== chatId)
        chats = [{ id: chatId, backend: backend, native: nativeId, title: title, ts: Date.now(), rows: rows }].concat(rest).slice(0, 40)
        store.setText(JSON.stringify({ chats: chats }))
    }
    function openChat(id) {
        const c = chats.find(c => c.id === id)
        if (!c || busy) return
        if (c.backend !== backend) Settings.data.aquilesBackend = c.backend
        messages.clear()
        for (const m of (c.rows || [])) row(m.kind, m.text, { reasoning: m.reasoning || "", name: m.name || "", detail: m.detail || "",
            result: m.result || "", state: m.state === "running" || m.state === "pending" ? "done" : (m.state || "") })
        cur = -1
        chatId = c.id; nativeId = c.native || ""; title = c.title || ""; usageText = ""
    }
    function deleteChat(id) {
        chats = chats.filter(c => c.id !== id)
        store.setText(JSON.stringify({ chats: chats }))
        if (id === chatId) newChat()
    }
    function renameChat(id, name) {
        chats = chats.map(c => c.id === id ? Object.assign({}, c, { title: name }) : c)
        store.setText(JSON.stringify({ chats: chats }))
        if (id === chatId) title = name
    }
}
