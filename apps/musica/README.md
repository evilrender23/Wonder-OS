# Música

The music player of Wonderlands OS: your own files, shown as a library. It is the desktop's default
app for audio — a song opened from Rabbit plays here.

- **Biblioteca**: Añadido recientemente, Artistas, Álbumes and Canciones, read from the tags of the
  files (MP3, FLAC, Ogg Vorbis, Opus, M4A/AAC, WAV, AIFF, WMA, APE, WavPack, Musepack) with their
  embedded covers. `~/Música` (the XDG music folder) is the library to begin with; the folder button
  under the sidebar adds or removes folders and rescans. A rescan only reads the files that changed.
- **Playback**: shuffle, repeat (queue or track), seek, volume, and «A continuación» — the queue,
  where a song can be played next, added at the end or taken out.
- **Playlists** and **Favoritas** (the heart on each song), kept as plain JSON.
- **Search** (Ctrl+F) across titles, artists and albums, without minding case or accents.
- **The desktop's media controls**: Música speaks MPRIS, so the bar's player, the media keys and
  `playerctl` see the track and its cover and can play, pause, skip and seek.
- One window: opening another song while Música runs plays it in the window already open.
- Light and dark follow the desktop, as does glass under WonderlandWM.

Keys: Space plays and pauses, Ctrl+→ / Ctrl+← next and previous, Ctrl+↑ / Ctrl+↓ volume, Ctrl+F
search, Ctrl+U the queue, Ctrl+N a new playlist, Ctrl+O add a folder, Alt+← back.

The library, playlists and settings live in `~/.local/share/wonderland/wonderland-musica/`.
Nothing is ever written to the music files.

## Build

Needs Qt ≥ 6.8 (base, declarative, svg, multimedia) and TagLib ≥ 2.0. On Arch:
`pacman -S cmake ninja qt6-base qt6-declarative qt6-svg qt6-multimedia qt6-multimedia-ffmpeg taglib`.

```sh
cmake -S apps/musica -B apps/musica/build -G Ninja
cmake --build apps/musica/build
ctest --test-dir apps/musica/build --output-on-failure
cmake --install apps/musica/build --prefix ~/.local
```

`./install.sh` at the top of the repository does this for every app, and makes Música the default
for audio files unless you already chose a player.

## Pictures of the window, without a desktop

```sh
wonderland-musica --appearance dark --shot out.png --do "page:albums;play:0;queue" ~/Música
```

`--do` takes `page:<recent|albums|artists|songs>`, `album:<n>`, `artist:<n>`, `play:<n>` (the nth
album), `queue`, `search:<text>`, `favorite:<n>`, `playlist:<name>`, `menu` and `name`.
`WONDERLAND_MUSICA_DATA` moves the data directory (so a test does not touch your library).

Icons: [Lucide](https://lucide.dev) (ISC), see `assets/symbols/LICENSE-lucide`.
