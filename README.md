<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo-white.svg">
    <img src="assets/logo-black.svg" alt="Logo de Wonder OS" width="200">
  </picture>
</p>

# Wonder OS

Wonder OS es una distribución Linux basada en CachyOS.

> [!NOTE]
> Wonder OS es un proyecto personal. No esperes soporte: eres libre de usarlo y
> lo seguiré actualizando activamente.

## Contenido del repositorio

- **WonderlandWM** (`src/`): el compositor Wayland de la distro, un fork de
  [driftwm](https://github.com/malbiruk/driftwm). Ver [NOTICE.md](NOTICE.md).
- **Apps** (`apps/`):
  - `bluetooth` — Bluetooth, gestor de dispositivos Bluetooth
  - `calendario` — Calendario
  - `fotos` — Fotos, biblioteca de imágenes y vídeos
  - `hojas` — Hojas
  - `musica` — Música, reproductor de música
  - `notas` — Notas
  - `peliculas` — Películas, reproductor de vídeo
  - `rabbit` — Rabbit, gestor de archivos
  - `sistema` — Sistema, ajustes del sistema
  - `wondermirror` — Wonder Mirror, aplicaciones de Windows como ventanas del escritorio
  - `wonderterm` — terminal
  - `wonderupdate` — WonderUpdate, el actualizador
- **Escritorio** (`extras/`): configuración, barra y shell en Quickshell, pantalla de inicio de
  sesión (`greeter`), pantalla de arranque (`plymouth`), scripts, temas y fondos de pantalla.
- **Sesión** (`resources/`): archivos de la sesión de inicio y servicios de systemd.

## Instalación

```bash
./install.sh
```

`./install.sh --help` muestra las opciones.

El instalador pone la pantalla de inicio de sesión de Wonder OS (`extras/greeter`) como gestor de
inicio de sesión desde el siguiente arranque. `./install.sh --no-greeter` deja el que haya, y
`extras/greeter/install.sh --disable` vuelve al anterior.

La pantalla de arranque con el logo de Wonder OS (un tema de Plymouth) se instala aparte con
`extras/plymouth/install.sh`; `extras/plymouth/install.sh --uninstall` vuelve a la anterior.

## Licencia

GPL-3.0-or-later. Ver [LICENSE](LICENSE) y [NOTICE.md](NOTICE.md).
