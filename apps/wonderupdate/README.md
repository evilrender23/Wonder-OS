# WonderUpdate

El actualizador de WonderlandOS. Hace para Wonderland lo que CachyUpdate
(`arch-update`) hace para CachyOS: busca actualizaciones, avisa, las aplica
desde una terminal y se ocupa de lo que una actualización deja pendiente.

Es Bash, sin compilación: solo necesita lo que el escritorio ya instala
(`pacman-contrib`, `curl`, `libnotify`). Los mensajes salen en español o en
inglés según el idioma del sistema.

## Qué actualiza

| Origen | Cómo se comprueba | Cómo se actualiza |
|---|---|---|
| Repositorios | `checkupdates` (sin root, no toca la base de datos de pacman) | `pacman -Syu` |
| AUR | `paru -Qua` / `yay -Qua` | `paru -Sua` / `yay -Sua` |
| Flatpak | `flatpak remote-ls --updates` (respeta `flatpak mask`) | `flatpak update` |
| Wonderland | `git fetch` en el checkout desde el que se instaló el escritorio | `git pull --ff-only` + `./install.sh --yes` |

El checkout de Wonderland solo avanza por *fast-forward*: si tiene commits
propios que no están en su origen, WonderUpdate avisa y no lo toca.

## Uso

```sh
wonderupdate              # actualización completa, con preguntas
wonderupdate --check      # comprobación desatendida: estado para la barra + notificación
wonderupdate --list       # qué hay pendiente (código de salida 7 si no hay nada)
wonderupdate --news 10    # últimas noticias de Arch
wonderupdate --services   # servicios que siguen usando archivos antiguos
wonderupdate --enable     # comprobación automática al iniciar sesión y cada hora
wonderupdate --gen-config # crea ~/.config/wonderupdate/wonderupdate.conf
```

`wonderupdate` sin opciones:

1. lista lo pendiente de cada origen y pide confirmación;
2. muestra las noticias de Arch publicadas desde la última vez (ahí se anuncian
   las actualizaciones que piden intervención manual);
3. actualiza repositorios, AUR, Flatpak y Wonderland, en ese orden. Si falla
   `pacman` se detiene; los demás orígenes son independientes entre sí;
4. ofrece eliminar paquetes huérfanos y runtimes de Flatpak sin usar, limpiar
   la caché de pacman (`paccache`), procesar los archivos pacnew (`pacdiff`) y
   reiniciar los servicios que lo necesitan (`checkservices`, del paquete
   `archlinux-contrib`, si está instalado);
5. avisa si hay que volver a entrar en la sesión (compositor actualizado) u
   ofrece reiniciar el equipo (kernel actualizado).

Nada se elimina ni se reinicia sin preguntar.

## En la barra

El widget `updates` de la barra usa WonderUpdate cuando está instalado: el
contador suma los cuatro orígenes, el panel los lista por separado y
«Update with WonderUpdate» abre `wonderupdate` en la terminal. La barra lee
`~/.local/state/wonderupdate/status.json`, que escribe cada comprobación, así
que también refleja las del timer y el final de una actualización.

Sin la barra, `wonderupdate --enable` activa un timer de systemd de usuario que
comprueba al iniciar sesión y cada hora. Cuando aparece algo nuevo se envía una
notificación con el botón «Actualizar ahora»; lo ya anunciado no se repite.

## Configuración

`wonderupdate --gen-config` copia [`wonderupdate.conf.example`](wonderupdate.conf.example)
a `~/.config/wonderupdate/wonderupdate.conf`; cada opción está explicada ahí
(orígenes desactivados, ayudante de AUR, orden para elevar privilegios, cuántas
versiones conservar en la caché, checkout de Wonderland, terminal…).

## Códigos de salida

| Código | Significado |
|---|---|
| 0 | correcto |
| 1 | opción desconocida |
| 2 | no hay orden para elevar privilegios |
| 3 | no se encontró una terminal (`--terminal`) |
| 4 | actualización cancelada por el usuario |
| 5 | ya hay otra actualización en marcha |
| 6 | algún origen falló al actualizar |
| 7 | `--list`: no hay actualizaciones |
| 8 | `--news`: no se pudieron obtener las noticias |
| 9 | `--enable` / `--disable`: systemd devolvió un error |
| 10 | error con el archivo de configuración |

## Instalación y pruebas

`./install.sh` (en la raíz del repositorio) lo instala con el resto de apps. A mano:

```sh
sudo -S -p '' make -C apps/wonderupdate install     # PREFIX=/usr/local por defecto
sudo -S -p '' make -C apps/wonderupdate uninstall
make -C apps/wonderupdate test
```

Las pruebas (`tests/run.sh`) ejecutan WonderUpdate contra versiones falsas de
pacman, paru, flatpak y demás, y un checkout de git desechable, con un `HOME`
propio: no leen ni cambian nada del sistema real.
