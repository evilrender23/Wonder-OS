# Calendario

The calendar of Wonderlands OS: events and appointments, kept on this computer. It is the desktop's
default app for `.ics` files — a calendar opened from Finder is added here.

- **Día, Semana, Mes and Año**: a day or a week on a grid of hours, with the events that overlap
  side by side and the ones that last the whole day above; a month of boxes; a year of twelve small
  months. The small month in the sidebar jumps to any day, and marks the days that have something.
- **Events**: title, place, notes, start and end (or the whole day, over as many days as it lasts),
  and a calendar. A double click on a day or an hour makes one there.
- **Repeats**: every day, week, month or year, for ever or until a day. One occurrence of a series
  can be taken out without touching the rest.
- **Calendars**: as many as you want, each with a name and a colour; the box beside each one shows
  or hides its events. A right click edits it, deletes it or makes it the one new events go to.
- **Reminders**: a notification from the desktop, from the moment of the event to a week before.
  They are sent while Calendario is open — there is no service in the background.
- **Search** (Ctrl+F) across titles, places and notes, without minding case or accents.
- **iCalendar**: imports `.ics` files (dropped on the window, opened from Finder, or from the «…»
  menu) and exports one calendar or all the visible ones, to take them to another app.
- One window: opening a file while Calendario runs adds it to the window already open.
- Light and dark follow the desktop, as does glass under WonderlandWM.

Keys: Ctrl+N a new event, Ctrl+T today, Ctrl+1 … Ctrl+4 day, week, month and year, ← / → the
previous and the next, Ctrl+F search.

Times are the ones on the wall: an event at nine is at nine wherever the computer is. Calendario
does not sync with any account; the calendars and events live in one JSON file in
`~/.local/share/wonderland/wonderland-calendario/`.

## Build

Needs Qt ≥ 6.8 (base, declarative, svg). On Arch: `pacman -S cmake ninja qt6-base qt6-declarative qt6-svg`.

```sh
cmake -S apps/calendario -B apps/calendario/build -G Ninja
cmake --build apps/calendario/build
ctest --test-dir apps/calendario/build --output-on-failure
cmake --install apps/calendario/build --prefix ~/.local
```

`./install.sh` at the top of the repository does this for every app, and makes Calendario the
default for `.ics` files unless you already chose an app for them.

## Pictures of the window, without a desktop

```sh
WONDERLAND_CALENDARIO_DATA=/tmp/cal wonderland-calendario --appearance dark --size 1120x720 \
    --shot out.png --do "sample;view:week"
```

`--do` takes `sample` (a fortnight of made-up events), `view:<day|week|month|year>`,
`day:<dd/mm/yyyy>`, `step:<n>`, `search:<text>`, `new`, `edit:<n>` (the nth event of the day),
`calendar`, `hide:<calendar id>`, `menu:<n>` and `say:<text>`. `WONDERLAND_CALENDARIO_DATA` moves
the data directory (so a test does not touch your calendars) and `WONDERLAND_CALENDARIO_NOW`
(`2026-11-10T10:30`) fixes the clock.

Icons: [Lucide](https://lucide.dev) (ISC), see `assets/symbols/LICENSE-lucide`.
