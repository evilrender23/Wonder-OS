# Wonder Mirror

Aplicaciones de Windows como ventanas del escritorio Wonderland, al estilo de
Parallels Coherence: Windows 11 corre en una máquina virtual QEMU/KVM en
segundo plano y cada una de sus apps aparece como una ventana más, con su
lanzador en el Launchpad y en Spotlight.

Es Python sin dependencias propias. Necesita `qemu-desktop`, `edk2-ovmf`,
`swtpm`, `freerdp` y `xwayland-satellite`, además de una imagen ISO de
Windows 11 que pones tú (Wonder Mirror no la descarga ni incluye licencia).

## Uso

```sh
wondermirror setup ~/Descargas/Windows11.iso   # instala Windows, sin preguntas (tarda)
wondermirror apps                              # qué apps tiene Windows
wondermirror open "Bloc de notas" [archivo]    # abre una app como ventana
wondermirror status                            # qué está haciendo Windows
wondermirror stop                              # apaga Windows
wondermirror keyboard [es|us|latam|auto|list]  # teclado con el que escriben las apps
wondermirror keyboard --pick                   # lo mismo, en una ventana
wondermirror config [clave] [valor]            # ajustes
wondermirror remove                            # borra Windows y todo lo suyo
```

Tras `setup`, cada app de Windows tiene un lanzador
(`~/.local/share/applications/wondermirror-app-*.desktop`); `wondermirror
refresh` los vuelve a generar.

## Ajustes

| Clave | Qué es | Por defecto |
|---|---|---|
| `cpus` | Procesadores de la máquina | `0`: automático |
| `memory_mb` | Memoria de la máquina | `0`: un cuarto de la del equipo, entre 4 y 16 GiB |
| `rdp_port` | Puerto local por el que se ven las ventanas | `33890` |
| `keyboard` | Distribución de teclado en Windows | vacío: la de este equipo |
| `gpu`, `gpu_card`, `gpu_memory_mb` | Tarjeta gráfica compartida (experimental, ver abajo) | apagada |

Los cambios de procesadores, memoria y tarjeta gráfica valen desde el
siguiente arranque de Windows (`wondermirror stop` y `wondermirror start`).

## Cómo funciona

- **La máquina** (`vm.py`): QEMU/KVM del usuario, sin libvirt, con firmware
  UEFI y TPM por software. Solo escucha en `127.0.0.1`. La instalación es
  desatendida (`unattend.py`, `guest/setup.ps1`) y crea una cuenta local con
  una contraseña aleatoria guardada en `account.json`, legible solo por ti.
- **Las ventanas** (`session.py`, `rdp.py`): una única conexión RDP RemoteApp
  con `xfreerdp3` para todas las apps, dentro de un servidor X privado
  (`xserver.py`), de modo que un fallo suyo no toca las apps X11 del
  escritorio. Dentro de Windows, `guest/agent.ps1` atiende las peticiones del
  equipo, mantiene el catálogo de apps y sus iconos (`guest/list-apps.ps1`) y
  cierra la sesión cuando se cierra la última ventana.
- **Datos**: la máquina vive en `~/.local/share/wondermirror`, los ajustes en
  `~/.config/wondermirror` y el estado que lee el escritorio (catálogo,
  iconos, registros) en `~/.local/state/wondermirror`.

## Instalación

`install.sh` del escritorio la instala con el resto de apps. A mano:

```sh
make install PREFIX=$HOME/.local
make install-freerdp PREFIX=$HOME/.local   # opcional, ver abajo
make test
```

`make uninstall` quita el programa y deja la máquina de Windows; esa la borra
`wondermirror remove`.

### Clic derecho: cliente FreeRDP parcheado

Con FreeRDP 3.32 de serie los menús contextuales de las apps de Windows no se
ven o los recibe otra ventana. `make install-freerdp` compila el cliente X11
de la misma versión que la del sistema con
`patches/freerdp-remoteapp-menus.patch` y lo instala solo para Wonder Mirror
(necesita `git`, `cmake` y `ninja`). Hay que repetirlo cuando el sistema
actualice FreeRDP: si las versiones no coinciden se usa el cliente de serie.

### xwayland-satellite

La versión 0.8.3 se cae al abrir ciertos menús con escala fraccionaria. Si hay
una compilación más reciente en `~/.local/lib/wondermirror/xwayland-satellite`
(o en `/usr/local/lib/wondermirror/`), Wonder Mirror usa esa.

## Tarjeta gráfica compartida (experimental)

`wondermirror config gpu on` añade a la máquina una tarjeta virtio-gpu con
Venus, que reenvía Vulkan a la tarjeta del equipo sin dedicársela. Hoy es una
prueba, no una función terminada:

- Windows necesita un controlador de terceros en fase alfa, firmado con un
  certificado de pruebas; `guest/gpu.ps1 -Action enable` lo instala desde
  `gpu-driver/` en el directorio de estado (no viene incluido) y activa el
  modo de firmas de prueba. `-Action disable` lo deshace.
- Solo sirve con tarjetas con controlador Vulkan de Mesa (AMD, Intel). El
  controlador propietario de NVIDIA no está soportado.
- Con ese controlador como tarjeta principal, las apps modernas de Windows
  (Configuración) colgaban el sistema. Por eso las sesiones dibujan por
  software y la tarjeta queda como segundo adaptador: solo la aprovechan los
  programas que la eligen expresamente.

Haz una copia de la máquina antes de probarlo.

## Límites conocidos

- Sin aceleración 3D útil (ver arriba).
- Con varios monitores, un menú contextual puede salir desplazado alguna vez.
- Las ventanas abiertas como administrador no cambian de teclado en vivo.
- `wondermirror screenshot` no funciona con la tarjeta compartida activa.
