# Bases

The database manager of Wonderlands OS's office suite: tables, queries, forms and reports over
plain SQLite files (`.db`, `.sqlite`, `.sqlite3`), which it is the desktop's app for. What Bases
adds to a file — saved queries, forms, reports, the configuration of each view — lives in
`_wonderland_*` tables of the same file, so the file stays one that any SQLite tool opens.

- **Sidebar and tabs**: the objects of the database grouped as Tablas, Consultas, Formularios and
  Informes, with a filter; each one opens in a tab.
- **One table, five ways** (Ctrl+1 … Ctrl+5): *Rejilla*, a grid that loads rows as they are
  scrolled to (hundreds of thousands are fine) and edits each cell by its type — text, number,
  date, checkbox, NULL, a drop-down for foreign keys; *Tarjetas*, a gallery; *Tablero*, a kanban
  grouped by a column, with the count of each group; *Formulario*, a record at a time; and
  *Diseño*, the columns, types, keys and indexes, which shows the SQL that saving will run.
  Each table remembers its view.
- **Filters as pills**: column · operator · value, as many as wanted, all of which must hold.
  Plus a search across every column and sorting by any header.
- **Record inspector** (Ctrl+I): the selected record field by field and the records of other
  tables tied to it by foreign keys, to walk along.
- **Queries**: an SQL editor with highlighting and completion of tables and columns
  (Ctrl+Space), `:parameters` that are asked for, results in a grid, the error SQLite gave when
  it fails, a history, and saved queries. A statement that fails is rolled back whole. *Constructor*
  builds a SELECT by ticking tables and columns, joined by their foreign keys.
- **Relaciones**: the tables as boxes and the foreign keys as lines; boxes are left where they
  are dragged.
- **Forms**: the fields wanted of a table and, below, the rows of the tables that refer to it.
- **Reports**: tabular or grouped, from a table or a query, with subtotals and totals, previewed
  page by page and exported to PDF.
- **Órdenes** (Ctrl+K): every action of the app, and every table, query, form and report, one
  search away — without minding case or accents.
- **Versions**: each Ctrl+S keeps a whole copy of the file (the last 20), which can be looked
  into and gone back to; going back keeps what there was as one more version.
- **Templates**: contactos, inventario, tareas, biblioteca, or an empty database.
- Each window is its own process with its own file. Light and dark follow the desktop, as does
  glass under WonderlandWM.

Keys: Ctrl+K orders, Ctrl+N / Ctrl+O / Ctrl+S new, open and save a version, Ctrl+T a new table,
Ctrl+Return a new record, Ctrl+D duplicate it, Ctrl+Backspace delete, Ctrl+Z / Ctrl+Shift+Z undo and
redo, Ctrl+F search, Ctrl+Shift+L a filter, Ctrl+1 … Ctrl+5 the views, Ctrl+R relations,
Ctrl+Shift+Return run the query, Ctrl+E export, Ctrl+B sidebar, Ctrl+I inspector, Ctrl+Shift+F focus
mode (Esc leaves it), Ctrl++ / Ctrl+- / Ctrl+0 zoom, Ctrl+Tab and Ctrl+W tabs, F5 refresh.

Every edit is written to the file as it is made, in a transaction — there is nothing to lose
between saves, so there is no recovery file; «saving» is what keeps a version. Undo covers the
edits of the session. A new database is made in the documents folder. Settings, recent files and
the versions (`versions/<document>/`) live in `~/.local/share/wonderland/wonderland-bases/`.

## Formats

| | Reads | Writes | Checked with |
|---|---|---|---|
| SQLite 3 | yes (the format it works on) | yes | `sqlite3` reads what Bases writes, tables and rows |
| CSV | import into a new table: separator, header and the type of each column are detected and can be changed | a table or the result of a query | round trip through LibreOffice Calc (`soffice --convert-to`) both ways |
| TSV | — | yes | not tried outside |
| JSON | — | an array of objects | parsed with Python |
| SQL | — | a table, or the whole database as a dump | the dump rebuilt with `sqlite3` |
| PDF | — | reports | opens as a valid PDF; not compared by eye with another viewer |

LibreOffice Base's `.odb` cannot be read: it embeds an HSQLDB or Firebird database, which needs
Java or Firebird itself. To bring such data over, export its tables to CSV from LibreOffice and
import them here. Spreadsheets (`.ods`, `.xlsx`) are not imported directly either. Everything goes
through quoted identifiers and bound values.

## Build

Needs Qt ≥ 6.8 (base with its SQLite driver, declarative, svg). On Arch:
`pacman -S cmake ninja qt6-base qt6-declarative qt6-svg`.

```sh
cmake -S apps/bases -B target/apps/bases -G Ninja
cmake --build target/apps/bases
ctest --test-dir target/apps/bases --output-on-failure
cmake --install target/apps/bases --prefix ~/.local
```

## Testing the window

`wonderland-bases --shot out.png --do "a;b:arg" [--size 1180x740] [--appearance light|dark] [file]`
runs the actions, saves a picture of the window and leaves. `WONDERLAND_BASES_DATA` moves the data
directory and `WONDERLAND_BASES_GLASS=0` turns the glass off. `--new <template|blank>` makes a
database and opens it.

An action is the id of any order of the palette, with its argument after a colon, or one of:

- Getting somewhere: `sample` (a shop: clientes, productos, pedidos, lineas_pedido, a few hundred
  rows), `blank:<name>`, `template:<id>`, `big:<rows>`, `table:<name>`, `query:<name>`,
  `formobject:<name>`, `report:<name>`, `relations`, `tab:<n>`, `palette:<text>`, `side:<filter>`.
- A table: `view:grid|cards|board[:column]|form|design`, `cell:<row>,<column>`, `type:<text>`,
  `edit`, `commit`, `null`, `toggle`, `addrow`, `delrow`, `duprow`, `sort:<column>`,
  `search:<text>`, `pill:<column>,<op>,<value>` (`pill` alone opens the editor), `unpill:<n>`,
  `clear`, `put:<field>=<value>`, `movecard:<from>,<card>,<to>`, `export:<path>`.
- Its design: `tablename:<name>`, `addfield:<name>`, `field:<n>`, `delfield`, `movefield`,
  `savetable`.
- A query: `text:<sql>`, `append:<sql>`, `run`, `param:<name>=<value>`, `save:<name>`, `builder`,
  `sql`, `addtable:<table>`, `criterion|sort|total|show:<table.column>=<value>`, `complete`,
  `accept`, `history`.
- A form or a report: `go:<n>`, `hide:<field>`, `sub:<n>`, `page:<n>`, `group:<column>`,
  `landscape`, `title:<text>`, `total:<column>=<sum|avg|count|min|max>`, `pdf:<path>`.
- Dialogs: `versions`, `version:pick:<n>|restore|remove|save`, `import:<csv>`,
  `importgo:name:<table>|sep:<c>|header:<0|1>|type:<column>=<TYPE>|skip:<column>|go`,
  `confirm:<table>`, `yes`, `sharemenu`, `newmenu`, `sidemenu:<name>`, `say:<text>`.
