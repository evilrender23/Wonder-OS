.pragma library

// The search of the sidebar: which pages, groups and settings of Schema.js a query finds.
// Case and accents do not count («raton» finds «Ratón»), and every word has to be there.

function fold(text) {
    return String(text === undefined || text === null ? "" : text).toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "")
}

function words(query) {
    return fold(query).split(/\s+/).filter(w => w !== "")
}

/// Whether every word is in `hay`, which is already folded.
function has(hay, ws) {
    return ws.every(w => hay.indexOf(w) >= 0)
}

function any(hay, ws) {
    return ws.some(w => hay.indexOf(w) >= 0)
}

/// What a setting can be found by: its name, its description, its config.toml key and its options.
function settingText(setting) {
    const options = (setting.options || []).map(o => o[1]).join(" ")
    return fold([setting.label, setting.desc, setting.path.join("."), setting.unit || "", options].join(" "))
}

/// `pages`: the pages whose name matches, as {index, title, icon}.
/// `groups`: the groups with something to show, as {page, group, pageTitle, title, items, table};
/// `items` are the settings found, and `table` says the group's table of bindings matched.
///
/// The page and group names count as context («zoom velocidad»), but a word has to be in the
/// setting or its group as well: the name of a page alone finds the page, not all it holds.
function find(pages, query) {
    const ws = words(query)
    const out = { pages: [], groups: [] }
    if (ws.length === 0)
        return out

    pages.forEach((page, index) => {
        const pageText = fold([page.title, page.id, page.keywords || ""].join(" "))
        if (has(pageText, ws))
            out.pages.push({ index: index, title: page.title, icon: page.icon })

        ;(page.groups || []).forEach((group, groupIndex) => {
            const groupText = fold(group.title)
            const context = pageText + " " + groupText
            const inGroup = any(groupText, ws)
            const items = (group.items || []).filter(setting => {
                const own = settingText(setting)
                return has(own + " " + context, ws) && (inGroup || any(own, ws))
            })
            let table = false
            if (group.bindings !== undefined) {
                const own = groupText + " " + fold([group.note || "", group.bindings.keyLabel, group.bindings.table.join(".")].join(" "))
                table = has(own + " " + pageText, ws) && any(own, ws)
            }
            if (items.length > 0 || table)
                out.groups.push({ page: index, group: groupIndex, pageTitle: page.title, title: group.title, items: items, table: table })
        })
    })
    return out
}
