# Wonder OS

Wonder OS es una distribución Linux basada en CachyOS.

> [!NOTE]
> Wonder OS es un proyecto personal. No esperes soporte: eres libre de usarlo y
> lo seguiré actualizando activamente.

## Contenido del repositorio

- **WonderlandWM** (`src/`): el compositor Wayland de la distro, un fork de
  [driftwm](https://github.com/malbiruk/driftwm). Ver [NOTICE.md](NOTICE.md).
- **Apps** (`apps/`):
  - `calendario` — Calendario
  - `finder` — Wonderland Finder, gestor de archivos
  - `fotos` — Fotos, biblioteca de imágenes y vídeos
  - `hojas` — Hojas
  - `musica` — Música, reproductor de música
  - `notas` — Notas
  - `peliculas` — Películas, reproductor de vídeo
  - `sistema` — Sistema, ajustes del sistema
  - `wondermirror` — Wonder Mirror, aplicaciones de Windows como ventanas del escritorio
  - `wonderterm` — terminal
  - `wonderupdate` — WonderUpdate, el actualizador
- **Escritorio** (`extras/`): configuración, barra y shell en Quickshell, pantalla de inicio de
  sesión (`greeter`), scripts, temas y fondos de pantalla.
- **Sesión** (`resources/`): archivos de la sesión de inicio y servicios de systemd.

## Instalación

```bash
./install.sh
```

`./install.sh --help` muestra las opciones.

## Licencia

GPL-3.0-or-later. Ver [LICENSE](LICENSE) y [NOTICE.md](NOTICE.md).
