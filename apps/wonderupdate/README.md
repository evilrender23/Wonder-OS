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
| Wonderland | la punta de la rama instalada, preguntada al repositorio | se compila sola en segundo plano; instalarla es `./install.sh --yes` con las mismas opciones |

## Actualización automática del escritorio

No hay versiones publicadas: cada equipo compila la punta de la rama desde la
que se instaló.

1. `./install.sh`, al terminar bien una instalación completa, anota qué dejó
   instalado en `~/.local/state/wonderlandwm/version.json`: commit, rama,
   repositorio y las opciones `--no-deps` / `--no-greeter` que se usaron.
2. Cada comprobación pregunta al repositorio por la punta de esa rama. Si no
   es el commit instalado, la versión nueva se descarga y se compila en segundo
   plano, con la prioridad más baja y sin root, en un clon propio
   (`~/.local/share/wonderos/source`). El checkout en el que trabajas no se
   toca nunca.
3. Instalar pide contraseña (`/usr/local`), así que eso espera: cuando la
   compilación termina llega una notificación «Wonder OS … está listo para
   instalar», y su botón abre una terminal con `wonderupdate --apply`, que solo
   instala lo ya compilado, con las mismas opciones de la instalación anterior
   (quien instaló con `--no-greeter` conserva su gestor de inicio de sesión).
   `wonderupdate` a secas la instala igual, junto con los paquetes.
4. El compositor nuevo entra al volver a iniciar sesión; el anterior queda en
   `~/.local/state/wonderlandwm/driftwm.prev`.

`~/.local/state/wonderupdate/auto.log` sigue el proceso con una línea por
suceso (`STEP n/3 …`, `READY <commit>`, `ERROR …`, `APPLIED <commit>`) y
`build.log` guarda la salida de la compilación.

Lo que no hace:

- Una instalación sin `version.json` (anterior a esto) sigue como antes: se
  actualiza con `git pull --ff-only` en el checkout del que salió, que solo
  avanza por *fast-forward*; si tiene commits propios, WonderUpdate avisa y no
  lo toca. Pasa al modo automático la próxima vez que se ejecute `./install.sh`.
- Una instalación de desarrollo (su commit no está en la rama publicada) no se
  actualiza sola.
- Si la compilación en segundo plano falla, no se reintenta para esa versión;
  la actualización se sigue ofreciendo y se puede instalar desde la terminal.
- `NoAutoUpdate` en la configuración desactiva la compilación en segundo plano
  (la actualización se sigue listando) y `AutoBuildInterval` fija las horas
  mínimas entre dos compilaciones (6 por defecto).

## Uso

```sh
wonderupdate              # actualización completa, con preguntas
wonderupdate --check      # comprobación desatendida: estado para la barra + notificación
wonderupdate --list       # qué hay pendiente (código de salida 7 si no hay nada)
wonderupdate --apply      # instala la versión nueva del escritorio, y nada más
wonderupdate --prepare    # la descarga y la compila sin instalar (lo que se hace solo)
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
| 11 | `--prepare` / `--apply`: la instalación no tiene `version.json` |

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
