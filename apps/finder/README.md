# Wonderland Finder

The file manager of Wonderlands OS: a Finder for WonderlandWM. Qt 6 / Qt Quick on top of KDE's
KIO, so it opens whatever KIO opens (local disks, the Trash, `smb://`, `sftp://`, MTP…).

## Every folder remembers how it looks

As macOS does with `.DS_Store`, each folder keeps:

- **where every icon is** — drag an icon anywhere and it stays there, across restarts;
- its **view** (icons, list or gallery), its **icon size** and how it is **kept**
  (free, snapped to a grid, or always sorted by name, kind, date or size).

Nothing is written inside your folders. A folder only carries an id in the extended attribute
`user.wonderland.folder-id`; its layout lives in
`~/.local/share/wonderland/wonderland-finder/folders/<id>.json`. So:

- a folder you **rename or move** keeps its layout;
- a folder you **copy** starts with the original's layout and goes its own way from the first change;
- folders that can't hold the attribute (read-only, FAT, network shares) are remembered by path.

A folder nobody arranged flows in rows, sorted by name. The first icon you move freezes the others
where they are. New files take the first free cell; nothing else moves. Right-click the background
for **Clean Up** (onto the grid, each icon near where it was), **Clean Up By**, **Sort By** and
**Reset Icon Positions**.

## Also

Sidebar with favourites, disks (mount / eject) and tags · back / forward · path bar · search ·
Quick Look (Space) · Get Info · rename, duplicate, alias, compress / extract · Trash with Put Back ·
undo · colour tags · drag and drop between windows and other apps · light and dark, following the
desktop · English and Spanish.

While it copies, moves or deletes, the top bar shows the progress in its centre: the operation, a bar
and how much is done ("1.2 GB of 4 GB"). Finder writes that state to
`$XDG_RUNTIME_DIR/wonderland/finder-ops.json`, which the bar watches; operations shorter than a blink
never show.

**Customize Folder…** (right-click a folder) gives it a colour and a symbol or an emoji on its front,
as Finder does on macOS. Every choice applies at once; **Reset** brings the plain folder back. The look
is kept on the folder itself (the `user.wonderland.folder` attribute), so it follows the folder when it
is moved or renamed, and the desktop icons draw it too: `wonderland-finder --customize FOLDER` opens
only that window, which is what the desktop's menu runs. The symbols are [Lucide](https://lucide.dev/)
icons (ISC).

Folders with no colour of their own take the **folder tint** chosen in Sistema («Carpetas»), kept in
`~/.config/wonderland/appearance.json` as `{"folder_tint": "#rrggbb"}`; without it they are the usual
blue. A new tint shows in the open windows at once.

**Quick Look**: Space on a selection opens a preview in a window of its own, sized to what it
shows: images and GIFs, text and source code, PDFs (every page), songs and films (they play at once,
with a bar to pause and seek), folders with their size, and for anything else its thumbnail or icon
with what is known of it. Space or Esc puts it away, the arrows walk the folder, Return opens the
item, and the button at the top opens it with its default app. The desktop's icons use the same
preview (`wonderland-finder --look FILE…`, or `Toggle` on `org.wonderland.Finder.QuickLook`).
PDFs need `qt6-webengine` (Qt PDF) and sound and video `qt6-multimedia`; without them those files
show as their thumbnail.

**Open With** makes the app you pick the default for that kind of file (it is written to
`~/.config/mimeapps.list`, so the rest of the desktop follows); the current default has a check mark.

## Build

Needs Qt ≥ 6.8 (base, declarative, svg, tools) and KDE Frameworks 6 (KIO, Solid, Service, Config,
CoreAddons). On Arch: `pacman -S cmake ninja qt6-base qt6-declarative qt6-svg qt6-tools kio solid kservice kconfig kcoreaddons`.

```sh
cmake -S apps/finder -B apps/finder/build -G Ninja
cmake --build apps/finder/build
ctest --test-dir apps/finder/build --output-on-failure
cmake --install apps/finder/build --prefix ~/.local
```

## Run

```sh
wonderland-finder [folders or files to reveal…]
wonderland-finder --view list --appearance dark ~/Downloads
```

One instance serves every window.

To make it the file manager of the session:

```sh
xdg-mime default wonderland-finder.desktop inode/directory
```

"Show in folder" in other apps goes to whoever owns `org.freedesktop.FileManager1` on the session
bus. The install adds a D-Bus service file so the session starts Wonderland Finder for it; installed
under `~/.local` it takes precedence over Dolphin's. A file manager that is already running keeps
the name until it quits.

For tests, `--shot out.png --do 'actions'` performs actions, saves a picture of the window and
quits. Actions are separated by `;` — for instance
`--do 'move:notes.txt:400:200;dump'` moves an icon and prints every icon's position.
