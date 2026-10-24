# GIP protocol notes

These are the notes gathered while reverse-engineering the **8BitDo Arcade Controller for Xbox** on macOS. They apply, for the most part, to any Xbox One / Series licensed wired controller, since all of them speak Microsoft's **GIP** (Gaming Input Protocol) over USB.

## USB identity

| Field | Value |
|---|---|
| Vendor ID | `0x2DC8` (8BitDo) |
| Product ID | `0x202C` |
| Product string | `8BitDo Arcade Controller for Xbox` |
| Device class / subclass / protocol | `0xFF` / `0x47` / `0xD0` (vendor-specific, GIP) |
| Speed | Full speed (12 Mbit/s) |

Interfaces:

| Interface | Alt | Endpoints | Purpose |
|---|---|---|---|
| 0 | 0 | `0x81` IN (interrupt, 64 B), `0x01` OUT (interrupt, 64 B) | GIP control and input |
| 1 | 0 | none | Audio (inactive) |
| 1 | 1 | `0x02` OUT (iso, 228 B), `0x83` IN (iso, 64 B) | Headset audio |

macOS ships no driver for the device class `0xFF/0x47/0xD0` from third-party vendors, so the device enumerates but **no configuration is selected** and nothing claims it. That is what makes a user-space driver with libusb possible: no kernel extension has to be unloaded or detached.

## Packet format

Every GIP message starts with a 4-byte header:

```
byte 0  command
byte 1  flags      0x20 = standard, 0x10 = the device expects an ACK, 0x30 = both
byte 2  sequence   incremented by the sender
byte 3  length     length of the payload that follows
```

## Start-up sequence

1. Select configuration 1 and claim interface 0.
2. The controller sends an **announce** packet (`0x02`), containing its MAC address, VID/PID and firmware versions:
   ```
   02 20 01 1c  <mac x6> 00 00  c8 2d  2c 20  <fw/hw versions...>
   ```
3. The host sends **power on** (`0x05`, payload `00`):
   ```
   05 20 <seq> 01 00
   ```
   Until this packet arrives, the controller does not send input reports.
4. From then on the controller sends input reports (`0x20`) and periodic status (`0x03`).

If the controller announces itself again (for example after a reset), power on must be sent again.

## Acknowledgements

When a received packet has the `0x10` flag, the host must answer with:

```
01 20 <seq of received packet> 09  00 <command> 20 <length> 00 00 00 00 00
```

## Input report (`0x20`)

Payload starts at byte 4 of the packet:

| Byte | Bits | Meaning |
|---|---|---|
| 4 | `0x04` | Menu |
| 4 | `0x08` | View |
| 4 | `0x10` / `0x20` / `0x40` / `0x80` | A / B / X / Y |
| 5 | `0x01` / `0x02` / `0x04` / `0x08` | D-pad up / down / left / right |
| 5 | `0x10` / `0x20` | LB / RB |
| 5 | `0x40` / `0x80` | LS click / RS click |
| 6–7 | uint16 LE | Left trigger (0–1023) |
| 8–9 | uint16 LE | Right trigger (0–1023) |
| 10–11 | int16 LE | Left stick X |
| 12–13 | int16 LE | Left stick Y (positive = up) |
| 14–15 | int16 LE | Right stick X |
| 16–17 | int16 LE | Right stick Y (positive = up) |

Depending on the controller's directional mode (D-pad / LS / RS) the four direction buttons are reported either as D-pad bits or as full-scale stick values. The driver treats both as the same `UP/DOWN/LEFT/RIGHT` inputs.

## Guide / Home button (`0x07`)

```
07 30 <seq> <len>  <state> ...
```

Bit 0 of byte 4 is the button state. This packet carries the ACK flag (`0x10`), so it must be acknowledged.

## Status / heartbeat (`0x03`)

```
03 20 <seq> 04  <status> 00 00 00
```

Sent periodically. The status byte was observed as `0x95` over cable and `0x85` through the wireless adapter. Its exact meaning is not documented here.

## Wired vs. 2.4 GHz wireless adapter

The 8BitDo wireless adapter enumerates with **exactly the same** VID, PID, product string and descriptors as the controller over cable, so the host cannot tell them apart from the USB descriptors. The only stable difference observed is the USB serial number, which contains the controller's MAC address:

| Connection | Serial number pattern |
|---|---|
| Cable | `XXXXXXXXXXXX0000` (ends in `0000`) |
| Wireless adapter | `0000XXXXXXXXXXXX` (starts with `0000`, the same string reversed) |

The driver uses that pattern to show the connection type. This heuristic was derived from a single unit, so reports from other units are welcome.
