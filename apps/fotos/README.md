# Fotos

La biblioteca de imágenes y vídeos de Wonderland: todo lo que hay en tus carpetas de
imágenes y vídeos, ordenado por fecha, sin importar nada ni mover ningún archivo.

- **Fototeca** por años, meses o días, con el tamaño de miniatura que elijas.
- **Favoritos**, **Vídeos**, **Capturas de pantalla** y **Carpetas**.
- **Álbumes**: reúnen fotos sin sacarlas de su carpeta.
- **Visor**: zum, giro, información (cámara, exposición, lugar), tira de miniaturas,
  pantalla completa y reproducción de vídeo.
- **Buscar** por nombre, carpeta, cámara o fecha («lisboa», «2024», «agosto»).
- Abre archivos sueltos (`wonderland-fotos foto.jpg`, arrastrar y soltar): una foto se
  abre junto a las demás de su carpeta, esté o no en la biblioteca. Una carpeta se añade
  a la biblioteca.
- Sigue el modo claro/oscuro y el cristal del escritorio. El cristal lo pone el compositor en
  las ventanas con regla `blur = true`; la de Fotos viene en `extras/config.toml`:

  ```toml
  [[window_rules]]
  app_id = "wonderland-fotos"
  blur = true
  corner_radius = 12
  shadow = true
  ```

Fotos no modifica tus archivos, con dos excepciones que pides tú: **girar** (en JPEG y
TIFF solo cambia la etiqueta de orientación; PNG y BMP se reescriben) y **mover a la
papelera**.

## Teclas

| | |
|---|---|
| `Ctrl+F` | Buscar |
| `Ctrl+O` | Añadir una carpeta a la biblioteca |
| `Ctrl+N` | Nuevo álbum (con lo seleccionado) |
| `Ctrl+A`, `Esc` | Seleccionar todo, soltar la selección |
| `Ctrl`/`Mayús` + clic | Seleccionar uno, seleccionar hasta aquí |
| `Ctrl++`, `Ctrl+-` | Miniaturas más grandes, más pequeñas |
| `F5` | Volver a leer la biblioteca |
| `←` `→` | Anterior, siguiente (visor) |
| `+` `-` `0` `1` | Zum, ajustar, tamaño real (visor) |
| `R`, `Mayús+R` | Girar a la derecha, a la izquierda |
| `L` | Favorito |
| `I` | Información |
| `F` | Pantalla completa |
| `Espacio` | Reproducir o pausar un vídeo |
| `Supr` | Mover a la papelera |

En el visor la rueda acerca y aleja; con `Mayús` pasa de foto.

## Compilar

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
sudo -S -p '' cmake --install build
```

Necesita Qt ≥ 6.8 (`qt6-base qt6-declarative qt6-svg qt6-multimedia`) y `exiv2` ≥ 0.28.
En ejecución usa `ffmpeg`/`ffprobe` para la miniatura y la duración de los vídeos, y
los formatos de imagen que Qt tenga instalados (`qt6-imageformats`, `kimageformats`
para HEIF, AVIF, JPEG XL o RAW). `install.sh`, en la raíz, lo instala todo.

## Dónde guarda las cosas

`~/.local/share/wonderland/wonderland-fotos/` (o `$WONDERLAND_FOTOS_DATA`):

- `settings.json` — carpetas de la biblioteca y preferencias;
- `albums.json` — álbumes y favoritos (listas de rutas);
- `library.json` — lo ya leído de cada archivo, para arrancar al instante;
- `thumbs/` — miniaturas; las que llevan 90 días sin usarse se borran solas.

Borrar esa carpeta devuelve Fotos al primer día sin tocar una sola foto.

## Otras aplicaciones

Una sola instancia: `org.wonderland.Fotos` en el bus de sesión, con `Open(as uris)`;
un segundo `wonderland-fotos` le pasa sus archivos a la ventana ya abierta.

## Capturas para pruebas

`wonderland-fotos --shot salida.png [--size 1120x720] [--appearance light|dark]
[--do "page:videos;open:0;info"]` dibuja la ventana, ejecuta las acciones y guarda una
imagen. Las acciones están en `runAction` de `qml/Main.qml`.

Los iconos son de [Lucide](https://lucide.dev) (ISC, `assets/symbols/LICENSE-lucide`).
