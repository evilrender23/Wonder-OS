# Barra «Rise» para WonderlandWM — plan y checklist

Origen: [HANCORE-linux/quickshell-dots](https://github.com/HANCORE-linux/quickshell-dots) («Quickshell Rise»,
MIT, `d0896fc`). Rama: `extras/barra-rise`. Quickshell 0.3.2.

## Decisiones

- **Una sola interfaz, no V1 + V2.** Rise mantiene dos UIs completas (~58k líneas) con estado separado.
  Aquí se porta el conjunto de funciones en una barra, con los estilos de widget de V2 como opciones.
- **Dónde vive:** barra fija arriba (layer-shell, `exclusiveZone`) en cada monitor **más** el dashboard
  actual anclado en el origen del canvas. Comparten servicios.
- **Estética Hermes** (tema `extras/themes/hermes`): paleta clara/oscura de Hermes Agent Desktop, radios de
  6 px, bordes de 1 px tintados de azul, acento Nous `#0053fd`.
- **Sin Hyprland ni Omarchy.** Cada dependencia se sustituye:

| Rise | WonderlandWM |
|---|---|
| `Quickshell.Hyprland` workspaces | Bookmarks del canvas por `ext-workspace-v1` (`Quickshell.WindowManager`) + `driftwm msg bookmark` |
| `Hyprland.dispatch(...)` | `driftwm msg action ...` |
| `omarchy-theme-set`, `omarchy-theme-bg-set` | Selector de temas de WonderlandWM: reescribe `[decorations]`/`[background]` de `config.toml` (hot-reload) y cambia la paleta de la barra |
| `omarchy-launch-*`, `omarchy-swayosd-*`, `omarchy-brightness-*` | `alacritty -e ...`, `swayosd-client`, `brightnessctl` |
| `omarchy-update` / scripts de Omarchy | `checkupdates` (pacman-contrib) + `paru`/`yay -Qua`, aplicar en terminal |
| swaync | Servidor de notificaciones propio de Quickshell (swaync sale del autostart) |

- **Lo que se queda fuera:** el actualizador de la propia barra (`qs-shell-*`: se actualiza con el repo),
  el controlador `qs-barctl` con unidades systemd transitorias (aquí la barra la lanza el compositor por
  `autostart`), voxtype y el speedtest de Cloudflare.
- **Código reutilizado de Rise** (MIT): scripts de uso de IA y la lógica de paneles que no depende de
  Hyprland, con aviso de copyright en `extras/quickshell/THIRD_PARTY.md`.

## Estructura

```
extras/quickshell/
├── shell.qml              ShellRoot { Dashboard; Bar por pantalla; Notificaciones; Selector }
├── services/              singletons: Theme, Canvas (IPC), Audio, Net, Bt, Power, Brightness,
│                          Media, SysStats, Weather, Notifs, AiUsage, Updates, Layout, Settings
├── bar/                   Bar.qml, BarSection.qml (arrastrar/soltar), widgets/*.qml
├── panels/                un panel por widget (popover anclado a la barra)
├── pickers/               temas y fondos (carrusel)
└── (dashboard actual)     Dashboard.qml y sus tiles, migrados a los servicios
extras/scripts/            claude-usage, codex-usage, opencode-usage, wonderland-updates
```

## Estado (verificado en sesión anidada, Quickshell 0.3.2)

Prueba: `dev/bench/qs_session.py` con los 21 widgets cargados y 17 paneles abiertos por IPC → **0 errores QML**
en el log de qs. Capturas reales con `drift-shot` (wlr-screencopy) en claro y oscuro.

### Hecho
- [x] Fundaciones: `Theme` (paleta Hermes clara/oscura), `Settings` (`~/.local/state/wonderlandwm/bar.json`,
      atómico, recarga si se edita a mano), `Compositor` (IPC: suscripción + petición/respuesta con reconexión),
      `Bar` por pantalla con 3 secciones y `exclusiveZone`; la central se aparta si no cabe
- [x] `IpcHandler "bar"`: `togglePanel <id>`, `toggleTheme`, `setTheme`, `toggleLayoutLock`, `resetLayout`,
      `setBookmarkStyle`
- [x] Sistema: reloj + calendario + zonas, audio (salida/entrada/apps), brillo, red (nmcli, contraseña, olvidar),
      Bluetooth, batería + perfil de energía, CPU (gráfica, hilos, temperatura, procesos con kill), memoria,
      tiempo (Open-Meteo), MPRIS (en línea + panel con seek y selector), bandeja con ocultables, inhibidor
- [x] Canvas: bookmarks (default/numbers/magic), panel con bookmarks + ventanas vivas y suspendidas,
      zoom, ventana enfocada, distribución de teclado
- [x] Notificaciones propias (historial, acciones, no molestar, toasts que se apartan si hay un panel abierto),
      uso de IA (scripts de Rise + timers), actualizaciones (checkupdates + paru/yay)
- [x] Personalización: arrastrar y soltar entre secciones, ocultar y restaurar, estilos por widget, selector de
      tema + carrusel de fondos aplicados al compositor por `wonderland-theme` (valida con `--check-config`
      antes de escribir; bloque gestionado, sin duplicados)
- [x] Integración: `extras/install.sh` (barra, scripts, tema), `extras/config.toml` (sin swaync, atajos
      `mod+space`, `mod+n`, `mod+,`, `mod+shift+b`, `mod+shift+t`), docs en `extras/quickshell/README.md`

### Diferencias con Rise (deliberadas o pendientes)
- Una interfaz, no V1 + V2. Sin las animaciones de *split/gap* de V1 ni los estilos Kanji/Frame/Aurora de V2.
- Estilo *full* de media = barra de progreso, no onda (Rise usa cava; sería una dependencia más).
- Selector de temas = tarjetas + carrusel; no los pickers Hearthstone/Tanzaku.
- Bookmarks por IPC del compositor, no `ext-workspace-v1` (el compositor aún no lo expone; es la Fase 3.6).
- Sin auto-actualizador de la barra (`qs-shell-*`) ni `qs-barctl`: la lanza el `autostart` del compositor.

### No verificado
- Interacción con ratón real (arrastre de widgets, clic en tarjetas): las capturas abren paneles por IPC.
- Toasts con el D-Bus real de la sesión: en la prueba, KDE ocupa `org.freedesktop.Notifications`; se probaron
  en un bus privado (`--notify`).
- Multi-monitor.
