# Bluetooth

The Bluetooth manager of Wonder OS: one window for the adapter and its devices.

- **The adapter**: on and off (it also lifts an rfkill block), its name, and whether other
  devices can see it.
- **Mis dispositivos**: the ones paired here, connected ones first, with their battery when
  they report it. Connect, disconnect, and a page of details per device: rename it, let it
  connect on its own («Conectar automáticamente», BlueZ's *trusted*), block it, forget it.
- **Dispositivos cercanos**: what is in range. It looks for devices only while the window is
  open. «Conectar» pairs, trusts and connects in one go; devices that have no name yet are left
  out until they give one.
- **Pairing questions**: while the app is open it is BlueZ's agent, so a code to compare, a code
  to type on a keyboard, a PIN to enter or a device asking to connect show up as a dialog.
- Failures are worded for the person («Comprueba que está encendido, cerca y en modo de enlace»)
  instead of BlueZ's error names.

It talks to BlueZ (`bluetooth.service`) through KDE's BluezQt, and changes nothing outside it.
The bar's Bluetooth panel opens it from «Bluetooth settings».

## Build

Needs Qt ≥ 6.8 (base, declarative, svg) and BluezQt. On Arch:
`pacman -S cmake ninja qt6-base qt6-declarative qt6-svg bluez bluez-qt`.

```sh
cmake -S apps/bluetooth -B apps/bluetooth/build -G Ninja
cmake --build apps/bluetooth/build
ctest --test-dir apps/bluetooth/build --output-on-failure
cmake --install apps/bluetooth/build --prefix ~/.local
```

## Trying it without touching the real Bluetooth

`wonderland-bluetooth --demo` runs on a made-up adapter with sample devices. The same backend
drives the tests (`tests/`), which cover the device list, its order and the steps of pairing.

Pictures, for checking a change: `--shot out.png` saves the window and quits; `--size 620x900`
and `--appearance light|dark` set it up, and `--do` runs actions first, separated by `;`:
`details:<address>`, `arm`, `connect:<address>`, `visible`, `end`, and with `--demo`
`sim:confirm|show|pin|authorize|off|none|empty|fail`.

```sh
QT_QPA_PLATFORM=offscreen wonderland-bluetooth --demo --shot /tmp/bt.png --do 'sim:confirm'
```

The symbols in `assets/symbols` are [Lucide](https://lucide.dev) icons (ISC, see
`LICENSE-lucide` there).
