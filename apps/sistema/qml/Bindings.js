.pragma library

// A bindings table of config.toml merges over the compositor's built-in bindings: an entry
// replaces the default on the same trigger, and "none" removes it.

const modifierOrder = ["super", "ctrl", "alt", "shift", "mod3"]
const modifierAliases = { logo: "super", control: "ctrl" }

/// A key combo as {mods, key}; `key` is empty for a tap binding (modifiers only).
function parseCombo(combo, modKey) {
    const parts = String(combo).split("+").map(p => p.trim()).filter(p => p.length > 0)
    const mods = []
    let key = ""
    parts.forEach((part, i) => {
        let name = part.toLowerCase()
        name = modifierAliases[name] || name
        if (name === "mod")
            name = modKey
        if (modifierOrder.indexOf(name) >= 0 && (i < parts.length - 1 || parts.every(p => isModifier(p)))) {
            if (mods.indexOf(name) < 0)
                mods.push(name)
        } else if (i === parts.length - 1) {
            key = part
        }
    })
    mods.sort((a, b) => modifierOrder.indexOf(a) - modifierOrder.indexOf(b))
    return { mods: mods, key: key }
}

function isModifier(part) {
    const name = part.toLowerCase()
    return name === "mod" || modifierOrder.indexOf(modifierAliases[name] || name) >= 0
}

/// One spelling per trigger, so "Mod+Q", "super+q" and "logo+Q" are the same binding.
function normalize(combo, modKey, canonicalKey) {
    const parsed = parseCombo(combo, modKey)
    const key = parsed.key ? (canonicalKey ? (canonicalKey(parsed.key) || parsed.key.toLowerCase()) : parsed.key.toLowerCase()) : ""
    return parsed.mods.concat(key ? [key] : []).join("+")
}

/// The combo as a binding spells it, preferring "mod" for the configured modifier.
function compose(mods, key, modKey) {
    const sorted = mods.slice().sort((a, b) => modifierOrder.indexOf(a) - modifierOrder.indexOf(b))
    return sorted.map(m => m === modKey ? "mod" : m).sort((a, b) => (a === "mod" ? -1 : 0) - (b === "mod" ? -1 : 0)).concat(key ? [key] : []).join("+")
}

const modifierGlyphs = { super: "❖", ctrl: "Ctrl", alt: "Alt", shift: "⇧", mod3: "Mod3" }
const keyGlyphs = {
    "return": "↵", "space": qsTr("Espacio"), "up": "↑", "down": "↓", "left": "←", "right": "→", "tab": "⇥", "escape": "Esc",
    "backspace": "⌫", "delete": qsTr("Supr"), "equal": "=", "minus": "−", "plus": "+", "comma": ",", "period": ".", "slash": "/",
    "print": qsTr("Impr Pant"), "prior": qsTr("Re Pág"), "next": qsTr("Av Pág"), "home": qsTr("Inicio"), "end": qsTr("Fin"), "insert": "Ins",
    "xf86audioraisevolume": "Vol +", "xf86audiolowervolume": "Vol −", "xf86audiomute": qsTr("Silencio"),
    "xf86monbrightnessup": qsTr("Brillo +"), "xf86monbrightnessdown": qsTr("Brillo −"),
    "xf86audioplay": "▶", "xf86audiopause": "⏸", "xf86audionext": "⏭", "xf86audioprev": "⏮", "xf86audiostop": "⏹"
}

/// The key caps to draw for a combo.
function caps(combo, modKey) {
    const parsed = parseCombo(combo, modKey)
    const out = parsed.mods.map(m => modifierGlyphs[m])
    if (parsed.key) {
        const lower = parsed.key.toLowerCase()
        out.push(keyGlyphs[lower] || (parsed.key.length === 1 ? parsed.key.toUpperCase() : parsed.key.replace(/^XF86/i, "")))
    }
    return out
}

/// The table as the compositor ends up with it.
///
/// `defaults` and `user` are [{key, value}]; `norm` maps a trigger to its one spelling.
/// A row: {id, key, value, state, defaultValue, userKey}, where state is
/// "default", "changed" (a default with another action), "off" (a default set to "none") or "custom".
function rows(defaults, user, norm, dropDefaults) {
    const out = []
    const byId = {}
    if (!dropDefaults) {
        defaults.forEach(entry => {
            const id = norm(entry.key)
            if (byId[id])
                return
            byId[id] = { id: id, key: entry.key, value: String(entry.value), state: "default", defaultValue: String(entry.value), userKey: "" }
            out.push(byId[id])
        })
    }
    user.forEach(entry => {
        const id = norm(entry.key)
        const value = String(entry.value)
        const row = byId[id]
        if (row && row.defaultValue !== undefined && row.state !== "custom") {
            row.userKey = entry.key
            if (value.trim() === "none") {
                row.state = "off"
            } else if (value !== row.defaultValue) {
                row.state = "changed"
                row.value = value
            }
        } else if (row) {
            // The same trigger written twice: the later line wins, as in the compositor.
            row.value = value
            row.userKey = entry.key
        } else if (value.trim() !== "none") {
            byId[id] = { id: id, key: entry.key, value: value, state: "custom", userKey: entry.key }
            out.push(byId[id])
        }
    })
    return out
}

function path(table, key) {
    return table.concat([key])
}

/// The changes that give `row` (or a new binding, when null) this trigger and action.
/// `taken` is the row already on that trigger, if any: it is replaced.
function save(table, row, key, value, norm, taken) {
    if (row && row.id === norm(key))
        return assign(table, row, key, value)
    return (row ? remove(table, row) : []).concat(assign(table, taken, key, value))
}

/// Puts an action on a trigger: over the row already there, or as a new line.
function assign(table, existing, key, value) {
    if (!existing)
        return [{ path: path(table, key), value: value, kind: "string" }]
    // Back on its built-in action: the override has nothing left to say.
    if (existing.defaultValue === value)
        return restore(table, existing)
    return [{ path: path(table, existing.userKey || existing.key), value: value, kind: "string" }]
}

/// Removing a custom binding deletes its line; a default one is switched off with "none".
function remove(table, row) {
    if (row.state === "custom")
        return [{ path: path(table, row.userKey), remove: true }]
    return [{ path: path(table, row.userKey || row.key), value: "none", kind: "string" }]
}

/// Back to the built-in binding.
function restore(table, row) {
    return row.userKey ? [{ path: path(table, row.userKey), remove: true }] : []
}
