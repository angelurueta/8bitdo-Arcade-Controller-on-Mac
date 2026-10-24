# 8BitDo Keys: driver para macOS del 8BitDo Arcade Controller for Xbox

[English](README.md) | **Español**

Un driver en espacio de usuario que permite usar el **[8BitDo Arcade Controller for Xbox](https://www.8bitdo.com/arcade-controller-xbox/)** en un Mac. Cada botón genera la tecla del teclado que tú elijas. Incluye una pequeña app nativa con un dibujo del control para reasignar las teclas de forma visual.

Funciona incluso con apps que leen el teclado directamente del hardware, como **OpenEmu**, porque las teclas salen por un teclado HID virtual real (el de Karabiner-Elements), no solo como pulsaciones simuladas.

<p align="center">
  <img src="docs/screenshot.png" alt="App 8BitDo Keys con el dibujo del control y los botones pulsados resaltados en verde" width="720">
</p>

> **Por qué existe:** los controles con licencia Xbox no usan HID USB estándar. Usan el protocolo **GIP** de Microsoft, y macOS solo es compatible con los controles de la propia Microsoft. El Mac detecta este control, pero ningún driver lo toma y ningún juego lo ve. Este proyecto habla GIP directamente por USB con libusb.

---

## Contenido

- [Características](#características)
- [Cómo funciona](#cómo-funciona)
- [Compatibilidad](#compatibilidad)
- [Requisitos](#requisitos)
- [Instalación](#instalación)
- [Uso](#uso)
- [Uso con OpenEmu](#uso-con-openemu)
- [Archivo de mapa de teclas](#archivo-de-mapa-de-teclas)
- [Solución de problemas](#solución-de-problemas)
- [Desinstalación](#desinstalación)
- [Estructura del proyecto](#estructura-del-proyecto)
- [Limitaciones](#limitaciones)
- [Licencia y créditos](#licencia-y-créditos)

---

## Características

- **Sin extensión de kernel**: funciona por completo en espacio de usuario con libusb.
- **App visual para reasignar teclas** (`8BitDo Keys.app`):
  - muestra un dibujo del control, y cada botón se ilumina al pulsarlo;
  - haz clic en un botón (o púlsalo en el control) y luego pulsa la tecla que quieres asignarle, o elígela de un menú;
  - indica si el control está conectado **por cable** o mediante el **transmisor inalámbrico de 2.4 GHz**;
  - indica si las teclas salen por el teclado virtual o como teclas simuladas, y permite pausar el envío.
- **Funciona con OpenEmu** y con otras apps que leen el teclado directamente del hardware, gracias al teclado HID virtual de Karabiner.
- **Modo alternativo** con teclas simuladas (CGEvent) cuando el teclado virtual no está disponible. Funcionan en la mayoría de las apps, pero no en OpenEmu.
- **Herramienta de terminal** (`gip2key`) que usa el mismo mapa de teclas que la app.
- **Herramienta de diagnóstico** (`probe`) que muestra los descriptores USB y los paquetes GIP en bruto.
- Se reconecta sola al desconectar y volver a conectar el control.

## Cómo funciona

```
┌────────────────────┐   USB (GIP)   ┌─────────────────────┐  Socket Unix  ┌──────────────────┐
│ 8BitDo Arcade      │ ────────────▶ │ 8BitDo Keys.app     │ ────────────▶ │ gip2key-relay    │
│ Controller / 2.4G  │               │  o gip2key (CLI)    │  tecla up/down│ (servicio root)  │
│ transmisor         │ ◀──────────── │  libusb + init GIP  │               └────────┬─────────┘
└────────────────────┘ power on/ACK  └──────────┬──────────┘                        │
                                                │ alternativo: CGEvent              ▼
                                                ▼                         Teclado HID virtual
                                          apps normales                   de Karabiner
                                                                                    │
                                                                                    ▼
                                                                    todas las apps, también OpenEmu
```

1. **La app (o la herramienta de terminal)** abre el control con libusb, le envía el comando GIP *power on*, responde los ACK que pide el control y decodifica los reportes de entrada. Los detalles están en [docs/PROTOCOL.md](docs/PROTOCOL.md) (en inglés).
2. Cada cambio de un botón se convierte en un evento **tecla abajo / tecla arriba** de la tecla asignada a ese botón.
3. Si el **relay** está activo, la tecla se le envía por un socket Unix en el que solo tu usuario puede escribir. El relay corre como root, porque el servicio del teclado virtual de Karabiner solo acepta clientes root, y escribe la tecla en el **teclado HID virtual de Karabiner**. Para macOS, ese teclado es un teclado físico real.
4. Si el relay no está activo, la tecla se **simula** con `CGEventPost`, lo que requiere el permiso de Accesibilidad.

## Compatibilidad

| Elemento | Probado |
|---|---|
| Control | 8BitDo Arcade Controller for Xbox (USB `2DC8:202C`) |
| Conexión | Cable USB-C y transmisor inalámbrico 8BitDo de 2.4 GHz |
| Mac | Apple Silicon (arm64) |
| macOS | 27 |
| Karabiner-Elements | Incluye Karabiner-DriverKit-VirtualHIDDevice **1.8.0** (biblioteca cliente `v8.7.0`) |

Otros controles Xbox con GIP probablemente funcionen cambiando `VID` / `PID` en `src/core/gip_core.c`. Usa `build/probe` para ver qué envía tu dispositivo.

## Requisitos

- macOS con las **Xcode Command Line Tools** (`clang`, `swiftc`, `git`)
- **[Homebrew](https://brew.sh)**
- **libusb** y **pkgconf**, instalados con Homebrew
- **[Karabiner-Elements](https://karabiner-elements.pqrs.org)**: necesario para OpenEmu y apps similares (recomendado)

---

## Instalación

### 1. Instala las herramientas de compilación

```sh
xcode-select --install          # omítelo si ya están instaladas
brew install libusb pkgconf
```

### 2. Descarga el código y compílalo

```sh
git clone https://github.com/angelurueta/8bitdo-Arcade-Controller-on-Mac.git
cd 8bitdo-Arcade-Controller-on-Mac
make
```

`make` compila todo en `build/`:

| Resultado | Qué es |
|---|---|
| `build/8BitDo Keys.app` | La app visual |
| `build/gip2key` | El driver de terminal |
| `build/gip2key-relay` | El servicio relay del teclado virtual |
| `build/probe` | La herramienta de diagnóstico USB/GIP |

La primera compilación descarga en `vendor/` la biblioteca cliente de Karabiner (solo headers, de dominio público). libusb se enlaza de forma estática, así que la app no depende de Homebrew para funcionar.

### 3. Instala Karabiner-Elements y aprueba su driver

Esto le da a macOS un teclado HID virtual. Solo hay que hacerlo una vez.

```sh
brew install --cask karabiner-elements
```

1. Abre **Karabiner-Elements** una vez.
2. Aprueba la extensión del driver. Ve a **Ajustes del Sistema → General → Inicio de sesión y extensiones → Extensiones de controlador** y activa **Karabiner-DriverKit-VirtualHIDDevice**. En algunas versiones de macOS el aviso aparece en cambio en **Ajustes del Sistema → Privacidad y seguridad**, con un botón **Permitir**.
3. En **Inicio de sesión y extensiones → Permitir en segundo plano**, comprueba que los elementos de Karabiner-Elements estén activados.
4. Concede **Monitoreo de entrada** a Karabiner si lo pide.

Comprueba que la extensión esté activa:

```sh
systemextensionsctl list | grep pqrs
# ... org.pqrs.Karabiner-DriverKit-VirtualHIDDevice (1.8.0/1.8.0) ... [activated enabled]
```

> **Versiones:** la versión de la biblioteca cliente debe coincidir con la del driver que instaló Karabiner-Elements. El número entre paréntesis (`1.8.0` arriba) es la versión del driver. La versión `v8.7.0` de [Karabiner-DriverKit-VirtualHIDDevice](https://github.com/pqrs-org/Karabiner-DriverKit-VirtualHIDDevice) trae el driver `1.8.0`. Si la tuya es otra, busca la etiqueta cuyo `version.json` tenga tu `driver_version`, pon esa etiqueta en `KARABINER_TAG` dentro del `Makefile` y vuelve a compilar con `make distclean && make`.

### 4. Instala el servicio relay

```sh
make install-relay        # ejecuta: sudo scripts/install-relay.sh
```

Esto instala `/usr/local/libexec/gip2key-relay` y el LaunchDaemon `/Library/LaunchDaemons/local.8bitdo.gip2key-relay.plist`. El servicio arranca al encender el Mac y se reinicia si se detiene. Su socket `/var/run/gip2key-relay.sock` pertenece a tu usuario, con permisos `0600`.

Revisa el log:

```sh
tail -5 /var/log/gip2key-relay.log
# escuchando en /var/run/gip2key-relay.sock
# conectado al servicio de Karabiner
# teclado virtual listo
```

### 5. Instala la app

```sh
make install              # copia la app a /Applications
```

Abre **8BitDo Keys** desde `/Applications`. La barra de estado superior debería mostrar:

- **Conectado por cable** o **Conectado por transmisor inalámbrico**: el control está conectado, por cable o mediante el transmisor.
- **Teclado virtual activo**: las teclas salen por el teclado virtual de Karabiner.

> **Sin el relay**, la app simula las teclas y necesita el permiso de **Accesibilidad**: **Ajustes del Sistema → Privacidad y seguridad → Accesibilidad → activa 8BitDo Keys**. La app está firmada localmente (ad hoc), así que macOS puede volver a pedir este permiso después de cada compilación. Con el relay activo, Accesibilidad no hace falta.

---

## Uso

### La app

- **Dibujo del control**: cada círculo es un botón, con su nombre (pequeño) y la tecla asignada (grande). El botón se pone **verde** mientras lo pulsas.
- **Reasignar un botón**:
  1. Haz clic en el botón del dibujo, o púlsalo en el control con la app al frente.
  2. Pulsa **Capturar tecla** y luego la tecla que quieras. También sirven los modificadores (⇧ ⌃ ⌥ ⌘).
  3. O elige la tecla en el menú desplegable. Elige **— ninguna —** para desactivar el botón.

  Los cambios se guardan al instante.
- **Fila "Stick derecho"**: teclas para las cuatro direcciones del stick derecho, que se usan cuando el modo de dirección del control está en RS.
- **Interruptor "Enviar teclas"**: pausa el envío de teclas, pero sigue mostrando las pulsaciones.
- **Restaurar predeterminados**: vuelve al mapa de teclas original.
- Cerrar la ventana cierra la app. Déjala abierta (o minimizada) mientras juegas.

Solo un programa puede usar el control a la vez, así que **no ejecutes la app y `gip2key` al mismo tiempo**.

### Terminal

```sh
build/gip2key            # ejecuta el driver
build/gip2key -v         # además muestra cada pulsación (útil para identificar botones)
build/gip2key -c mi.conf # usa otro archivo de mapa de teclas
```

Por defecto `gip2key` usa el mismo mapa de teclas que la app, `~/Library/Application Support/8BitDo Keys/keymap.conf`. Si ese archivo aún no existe, usa el mapa predeterminado.

### Diagnóstico

```sh
build/probe
```

Muestra las interfaces y endpoints USB, envía *power on* y luego muestra durante 20 segundos cada paquete GIP recibido. Úsalo para añadir compatibilidad con otro control o para reportar un problema.

---

## Uso con OpenEmu

1. Comprueba que el **relay** esté instalado y que la app muestre **Teclado virtual activo**.
2. Dale a OpenEmu el permiso de **Monitoreo de entrada**: **Ajustes del Sistema → Privacidad y seguridad → Monitoreo de entrada → OpenEmu**.
3. Abre **OpenEmu → Ajustes → Controles**, elige un sistema, selecciona **Teclado** como dispositivo de entrada, y luego haz clic en cada acción y pulsa el botón correspondiente del control.

OpenEmu lee los teclados mediante `IOHIDManager`, así que ignora las pulsaciones simuladas con `CGEvent`. Por eso hacen falta el relay y el teclado virtual de Karabiner. Los emuladores que aceptan pulsaciones normales (RetroArch, MAME, …) también funcionan sin el relay.

---

## Archivo de mapa de teclas

La app guarda el mapa de teclas en `~/Library/Application Support/8BitDo Keys/keymap.conf`. También puedes editarlo a mano. [config/keymap.example.conf](config/keymap.example.conf) contiene los valores predeterminados.

```ini
# BOTON = tecla      ("none" desactiva el botón)
UP    = up
A     = z
MENU  = return
LS    = none
```

**Botones:** `A B X Y LB RB LT RT MENU VIEW HOME LS RS UP DOWN LEFT RIGHT RUP RDOWN RLEFT RRIGHT`

**Teclas:**

| Grupo | Nombres |
|---|---|
| Letras y números | `a`–`z`, `0`–`9` |
| Flechas | `up down left right` |
| Especiales | `space return tab escape backspace capslock` |
| Modificadores | `shift control option command rshift rcontrol roption` |
| Puntuación | `minus equal leftbracket rightbracket backslash semicolon quote comma period slash grave` |
| Teclas de función | `f1`–`f12` |
| Cualquier otro código de tecla virtual de macOS | `kNN`, p. ej. `k82` (solo teclas simuladas; no pasa por el relay) |

También se aceptan los alias `enter`, `esc`, `alt`, `ctrl`, `cmd`, `ralt` y `rctrl`.

**Mapa predeterminado:**

| Control | Tecla | Control | Tecla |
|---|---|---|---|
| Arriba / Abajo / Izquierda / Derecha | Flechas | Menu | Return |
| A / B / X / Y | Z / X / A / S | View | Tab |
| LB / RB / LT / RT | Q / W / E / R | Home | Escape |
| Stick derecho ↑ ↓ ← → | I / K / J / L | LS / RS | — |

---

## Solución de problemas

| Síntoma | Causa / solución |
|---|---|
| La app dice **Control no detectado** | Revisa el cable o el transmisor con `ioreg -p IOUSB -w0 \| grep -i 8bitdo`. Comprueba que `gip2key` o `probe` no estén corriendo al mismo tiempo. |
| Los botones se iluminan pero no se escriben teclas | Sin el relay: concede **Accesibilidad** a la app (de nuevo tras compilar). Con el relay: revisa su log. |
| Las teclas funcionan en TextEdit pero **no en OpenEmu** | El relay no está activo. Instálalo (paso 4) y dale a OpenEmu **Monitoreo de entrada**. |
| Log del relay: `no se pudo conectar a Karabiner: asio.system:2` | El socket del servicio de Karabiner aún no existe. La extensión del driver no está aprobada (`systemextensionsctl list` muestra `activated waiting for user`) o Karabiner-Elements no está corriendo. Apruébala (paso 3). El relay se reintenta solo. |
| Log del relay: `la versión del driver de Karabiner no coincide` | La biblioteca cliente no coincide con tu driver de Karabiner. Mira *Versiones* en el paso 3. |
| Un botón aparece en el lugar equivocado del dibujo | La distribución es aproximada. Ejecuta `build/gip2key -v` para ver qué nombre reporta cada botón y abre un issue. |
| La conexión dice **tipo desconocido** | El número de serie USB de tu unidad no sigue el patrón observado. Abre un issue con el número de serie (aparece al pasar el puntero sobre el estado). |

---

## Desinstalación

```sh
make uninstall-relay                       # elimina el servicio y su LaunchDaemon
rm -rf "/Applications/8BitDo Keys.app"
rm -rf ~/Library/Application\ Support/8BitDo\ Keys
```

Karabiner-Elements se desinstala desde su propio menú (**Uninstall Karabiner-Elements**) o con `brew uninstall --cask karabiner-elements`.

---

## Estructura del proyecto

```
.
├── Makefile                    compilación, instalación y objetivos del relay
├── src/
│   ├── core/gip_core.{c,h}     GIP sobre libusb, decodificación de botones, mapa y salida de teclas
│   ├── app/main.swift          app SwiftUI (8BitDo Keys)
│   ├── app/Info.plist
│   ├── cli/gip2key.c           driver de terminal
│   ├── relay/gip2key-relay.cpp servicio root → teclado HID virtual de Karabiner
│   └── tools/probe.c           volcado de paquetes USB / GIP
├── scripts/install-relay.sh    instala o desinstala el LaunchDaemon
├── config/keymap.example.conf  mapa de teclas predeterminado
└── docs/PROTOCOL.md            notas del protocolo GIP de este control (en inglés)
```

`build/` y `vendor/` se generan al compilar y git los ignora.

## Limitaciones

- **Solo salida de teclado.** macOS solo permite crear gamepads virtuales a apps con un permiso que concede Apple (`com.apple.developer.hid.virtual.device`), así que el control no puede aparecer como gamepad en los juegos.
- Con el transmisor inalámbrico, la app todavía no distingue si el control está apagado: el transmisor solo ya aparece como "conectado".
- El dibujo del control es una aproximación de la distribución leverless.
- No hay soporte para vibración, audio ni las funciones de perfiles propias del control.

## Licencia y créditos

- Este proyecto se publica bajo la [licencia MIT](LICENSE).
- [libusb](https://libusb.info) tiene licencia LGPL-2.1. La app la enlaza de forma estática. Si redistribuyes binarios compilados, ten en cuenta las obligaciones de reenlazado de la LGPL.
- [Karabiner-DriverKit-VirtualHIDDevice](https://github.com/pqrs-org/Karabiner-DriverKit-VirtualHIDDevice), de Takayama Fumihiko, es de dominio público y se descarga al compilar.
- El conocimiento del protocolo GIP se apoya en el trabajo de los drivers de Linux [`xpad`](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c) y [`xone`](https://github.com/medusalix/xone).
- Este proyecto no está afiliado a 8BitDo, Microsoft ni al proyecto Karabiner. "Xbox" es una marca registrada de Microsoft.
