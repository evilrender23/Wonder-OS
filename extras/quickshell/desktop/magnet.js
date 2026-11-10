.pragma library

// Free placement with a magnetic feel, for the desktop icons.
//
// There is no global grid: an icon stays wherever it is dropped. While it is
// dragged, nearby icons and the screen margins attract it — onto a neighbour's
// row or column, or one cell away from it — so tidy rows grow on their own.
//
// Positions are a cell's top-left corner, in px inside the icon field.
// cfg: { cellW, cellH, width, height, margin, radius, capture, reach, magnet }

// Offset from a target for a raw offset `u` inside the field of radius `r`:
// flat next to the target (it sticks), then eases out to meet the free motion
// at the rim with the same slope, so there is no jump on the way in or out.
function shape(u, r) {
    const t = Math.min(1, Math.abs(u) / r)
    return u * (2 * t - t * t)
}

function clamp(p, cfg) {
    return {
        x: Math.max(0, Math.min(cfg.width - cfg.cellW, p.x)),
        y: Math.max(0, Math.min(cfg.height - cfg.cellH, p.y))
    }
}

function overlaps(a, b, cfg) {
    return Math.abs(a.x - b.x) < cfg.cellW * 0.8 && Math.abs(a.y - b.y) < cfg.cellH * 0.8
}

function isFree(p, others, cfg) {
    for (const o of others) if (overlaps(p, o, cfg)) return false
    return true
}

// What pulls on each axis at `raw`: a neighbour's own row/column ("align"),
// the next cell beside or below it ("gap"), and the screen margins ("edge").
// Only neighbours within `reach` cells count, so a crowded desktop doesn't
// turn into wall-to-wall stickiness.
function targets(raw, others, cfg) {
    const xs = [], ys = []
    const reach = cfg.reach || 3.5
    for (const o of others) {
        const dx = Math.abs(raw.x - o.x), dy = Math.abs(raw.y - o.y)
        if (dy < cfg.cellH * reach) xs.push({ v: o.x, kind: "align", o: o })
        if (dx < cfg.cellW * reach) ys.push({ v: o.y, kind: "align", o: o })
        if (dy < cfg.cellH * 0.75) {
            xs.push({ v: o.x + cfg.cellW, kind: "gap", o: o })
            xs.push({ v: o.x - cfg.cellW, kind: "gap", o: o })
        }
        if (dx < cfg.cellW * 0.75) {
            ys.push({ v: o.y + cfg.cellH, kind: "gap", o: o })
            ys.push({ v: o.y - cfg.cellH, kind: "gap", o: o })
        }
    }
    xs.push({ v: cfg.margin, kind: "edge" })
    xs.push({ v: cfg.width - cfg.cellW - cfg.margin, kind: "edge" })
    ys.push({ v: cfg.margin, kind: "edge" })
    ys.push({ v: cfg.height - cfg.cellH - cfg.margin, kind: "edge" })
    return { xs: xs, ys: ys }
}

function nearest(list, v, r) {
    let best = null, bestD = r
    for (const t of list) {
        const d = Math.abs(t.v - v)
        if (d < bestD) { best = t; bestD = d }
    }
    return best
}

// The position to show for a dragged icon whose pointer puts it at `raw`,
// plus the alignment guides to draw and whether each axis would stick on drop.
function magnetize(raw, others, cfg) {
    if (!cfg.magnet) {
        const p = clamp(raw, cfg)
        return { x: p.x, y: p.y, guides: [], lockX: false, lockY: false }
    }
    const t = targets(raw, others, cfg)
    const tx = nearest(t.xs, raw.x, cfg.radius)
    const ty = nearest(t.ys, raw.y, cfg.radius)
    const p = clamp({
        x: tx ? tx.v + shape(raw.x - tx.v, cfg.radius) : raw.x,
        y: ty ? ty.v + shape(raw.y - ty.v, cfg.radius) : raw.y
    }, cfg)
    const guides = []
    const over = 10
    if (tx && tx.kind === "align") {
        guides.push({
            vertical: true,
            pos: tx.v + cfg.cellW / 2,
            from: Math.min(tx.o.y, p.y) - over,
            to: Math.max(tx.o.y, p.y) + cfg.cellH + over,
            strength: 1 - Math.abs(raw.x - tx.v) / cfg.radius
        })
    }
    if (ty && ty.kind === "align") {
        guides.push({
            vertical: false,
            pos: ty.v + cfg.cellH / 2,
            from: Math.min(ty.o.x, p.x) - over,
            to: Math.max(ty.o.x, p.x) + cfg.cellW + over,
            strength: 1 - Math.abs(raw.y - ty.v) / cfg.radius
        })
    }
    const cap = cfg.radius * cfg.capture
    return {
        x: p.x, y: p.y, guides: guides,
        lockX: !!tx && Math.abs(raw.x - tx.v) < cap,
        lockY: !!ty && Math.abs(raw.y - ty.v) < cap
    }
}

// Two icons are stacked when one hides most of the other. Anything looser is
// the user's business: icons may sit close together or partly overlap.
function stacked(a, b, cfg) {
    return Math.abs(a.x - b.x) < cfg.cellW * 0.4 && Math.abs(a.y - b.y) < cfg.cellH * 0.4
}

// `p`, nudged just far enough that it is stacked on no other icon. It moves
// along whichever axis needs the smaller step, away from the icon in the way,
// or to its other side when the screen ends there. With the magnet off
// placement is free: the icon stays where it is put, on top of another or not.
function resolve(p, others, cfg) {
    p = clamp(p, cfg)
    if (!cfg.magnet) return p
    const sx = cfg.cellW * 0.4, sy = cfg.cellH * 0.4
    for (let pass = 0; pass < 8; pass++) {
        let blocker = null, blockerD = Infinity
        for (const o of others) {
            if (!stacked(p, o, cfg)) continue
            const d = Math.hypot(p.x - o.x, p.y - o.y)
            if (d < blockerD) { blocker = o; blockerD = d }
        }
        if (!blocker) return p
        const dx = p.x - blocker.x, dy = p.y - blocker.y
        if (sx - Math.abs(dx) <= sy - Math.abs(dy)) {
            let x = blocker.x + (dx >= 0 ? sx : -sx)
            if (x < 0 || x > cfg.width - cfg.cellW) x = blocker.x + (dx >= 0 ? -sx : sx)
            p = { x: x, y: p.y }
        } else {
            let y = blocker.y + (dy >= 0 ? sy : -sy)
            if (y < 0 || y > cfg.height - cfg.cellH) y = blocker.y + (dy >= 0 ? -sy : sy)
            p = { x: p.x, y: y }
        }
    }
    return p
}

// Where a dragged icon lands. With the magnet off, exactly where it is
// dropped. With it on: on the target when the drop is inside the capture
// radius, otherwise where it was shown, and never stacked on another icon.
function settle(raw, others, cfg) {
    const m = magnetize(raw, others, cfg)
    const p = { x: m.x, y: m.y }
    if (cfg.magnet) {
        const t = targets(raw, others, cfg)
        const cap = cfg.radius * cfg.capture
        const tx = nearest(t.xs, raw.x, cap)
        const ty = nearest(t.ys, raw.y, cap)
        if (tx) p.x = tx.v
        if (ty) p.y = ty.v
    }
    return resolve(p, others, cfg)
}

// The lattice new icons fill, macOS-style: columns from the top-right corner,
// each one top to bottom.
function columns(cfg) { return Math.max(1, Math.floor((cfg.width - 2 * cfg.margin) / cfg.cellW)) }
function rows(cfg) { return Math.max(1, Math.floor((cfg.height - 2 * cfg.margin) / cfg.cellH)) }
function slot(c, r, cfg) {
    return { x: cfg.width - cfg.margin - (c + 1) * cfg.cellW, y: cfg.margin + r * cfg.cellH }
}

function freeSlot(others, cfg) {
    const nc = columns(cfg), nr = rows(cfg)
    for (let c = 0; c < nc; c++) {
        for (let r = 0; r < nr; r++) {
            const s = slot(c, r, cfg)
            if (isFree(s, others, cfg)) return s
        }
    }
    return clamp(slot(0, 0, cfg), cfg)
}

// names in order -> { name: {x, y} } filling the lattice.
function arrange(names, cfg) {
    const out = {}, placed = []
    for (const n of names) {
        const s = freeSlot(placed, cfg)
        out[n] = s
        placed.push(s)
    }
    return out
}

// "Clean up": every icon moves to the lattice slot nearest to it. Icons are
// taken right to left, top to bottom, so the ones already in place keep it.
function cleanUp(items, cfg) {
    const nc = columns(cfg), nr = rows(cfg)
    const taken = {}, out = {}
    const sorted = items.slice().sort((a, b) => (b.x - a.x) || (a.y - b.y))
    for (const it of sorted) {
        let best = null, bestD = Infinity, bestKey = ""
        for (let c = 0; c < nc; c++) {
            for (let r = 0; r < nr; r++) {
                const key = c + ":" + r
                if (taken[key]) continue
                const s = slot(c, r, cfg)
                const d = Math.hypot(s.x - it.x, s.y - it.y)
                if (d < bestD) { best = s; bestD = d; bestKey = key }
            }
        }
        if (!best) best = clamp(slot(0, 0, cfg), cfg)
        else taken[bestKey] = true
        out[it.name] = best
    }
    return out
}
