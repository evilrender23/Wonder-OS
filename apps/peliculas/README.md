# Películas

El reproductor de vídeo de WonderlandOS: abre un archivo, varios o una carpeta
entera y los pone uno detrás de otro, con sus subtítulos, sus pistas de sonido
y el punto donde se dejó cada uno.

Es Qt 6 (QML + C++) sobre QtMultimedia con FFmpeg, así que reproduce lo que
FFmpeg reproduce: MP4, MKV, WebM, MOV, AVI, MPEG-TS… y también sonido suelto
(MP3, FLAC, Ogg, Opus, M4A, WAV).

## Qué hace

- **Abrir**: desde Rabbit (es la app para vídeos), arrastrando a la ventana,
  con «Abrir archivo…» / «Abrir carpeta…» o desde la terminal. Un solo archivo
  trae consigo a sus vecinos de carpeta, en orden natural («Capítulo 2» antes
  que «Capítulo 10»), para poder pasar al siguiente.
- **Continuar viendo**: recuerda por dónde iba cada película y sigue desde ahí.
  La primera página lista lo último que se vio, con lo que queda.
- **Subtítulos**: los que lleva el archivo y los `.srt`, `.vtt`, `.ass` y `.ssa`
  que están a su lado con el mismo nombre («Film.srt», «Film.en.srt»); también
  se puede añadir cualquier otro archivo.
- **Pistas de sonido**: si hay varias, se elige cuál, con el idioma por su nombre.
- **Velocidad** de 0,5× a 2×, **tamaño de la imagen** (ajustar, llenar, estirar)
  y **captura** del fotograma a la carpeta Imágenes.
- **Lista**: lo que hay en cola, con orden aleatorio y repetir la lista o uno.
- **Pantalla completa**, con los controles y el puntero que se retiran solos.
- **MPRIS**: las teclas multimedia y el panel de la barra lo manejan, y una
  segunda copia entrega sus archivos a la que ya está abierta.

## Teclas

| Tecla | Acción |
|---|---|
| Espacio, K | Reproducir / pausa |
| ← → | 10 s atrás / adelante (con Mayús, 1 min) |
| ↑ ↓, rueda | Volumen |
| M | Silenciar |
| F, F11, doble clic | Pantalla completa |
| N, P | Siguiente / anterior |
| L | Lista |
| V | Cambiar de subtítulos |
| [ ] , Retroceso | Más lento / más rápido, velocidad normal |
| S | Guardar captura |
| Ctrl+O, Ctrl+Mayús+O | Abrir archivo / carpeta |
| Ctrl+W | Cerrar la película |

## Compilar

```sh
sudo pacman -S --needed cmake ninja qt6-base qt6-declarative qt6-svg qt6-multimedia qt6-multimedia-ffmpeg
cmake -S apps/peliculas -B apps/peliculas/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build apps/peliculas/build
ctest --test-dir apps/peliculas/build --output-on-failure
```

`./install.sh` la compila e instala con el resto de apps y la deja como la app
para vídeos, salvo que ya se hubiera elegido otra.

## Uso

```sh
wonderland-peliculas                      # la primera página
wonderland-peliculas film.mkv             # un archivo (y sus vecinos en la lista)
wonderland-peliculas ~/Vídeos/Serie       # una carpeta
wonderland-peliculas --appearance dark    # auto (como el escritorio), light o dark
```

El historial está en `~/.local/share/wonderland/wonderland-peliculas/history.json`.

## Por dentro

| Pieza | Qué es |
|---|---|
| `src/media.*` | Qué es reproducible, títulos, tiempos, orden natural, nombres de idioma |
| `src/playlist.*` | La lista: orden, aleatorio, repetir |
| `src/history.*` | Dónde se dejó cada película |
| `src/subtitles.*` | Lectura de SRT, VTT y ASS, y los subtítulos que acompañan a un archivo |
| `src/player.*` | Une todo lo anterior con QtMultimedia |
| `src/mpris.*` | MPRIS e instancia única |
| `qml/` | La ventana: primera página, imagen con sus barras, lista |

Los iconos son de [Lucide](https://lucide.dev) (ISC, ver `assets/symbols/LICENSE-lucide`).
