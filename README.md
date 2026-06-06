# nRF52840 9-DoF Receiver

FThis is s firmware for the **receiver dongle** of a wireless full-body VR tracker system.

The whole project is made up of three repositories:

| Component | Repo | Role |
|-----------|------|------|
| Transmitter | [nrf52840_9dof_transmitter](https://github.com/Glebhl/nrf52840_9dof_transmitter) | Worn tracker(s) |
| **Receiver / USB dongle (this repo)** | [nrf52840_9dof_receiver](https://github.com/Glebhl/nrf52840_9dof_receiver) | Listens to all trackers and bridges packets to USB |
| SteamVR driver | [nrf52840_steamvr_driver](https://github.com/Glebhl/nrf52840_steamvr_driver) | OpenVR driver for SteamVR |

```
  ┌──────────────┐                    ┌────────────┐                 ┌──────────────┐
  │  Tracker(s)  │   2.4 GHz radio    │  Receiver  │   USB serial    │ SteamVR      │
  │  (this repo) │  ───────────────>  │  dongle    │  ────────────>  │ driver (DLL) │
  └──────────────┘                    └────────────┘                 └──────────────┘
```

> ⚠️ **Status: work in progress.** Not all functionality is implemented and the
> system is not yet fully working end to end.

## What it does

The reciver is keeps up with a stream of packets and pushes them to the driver
via USB

## Building & flashing

This is a [PlatformIO](https://platformio.org/) project.

```bash
# Build
pio run

# Put the controller into DFU mode and upload the code
python gen_uf2.py

```

## Serial protocol (RX → host)

The receiver bridges the nRF radio to USB serial. To parse packets on a host,
build the receiver with **binary mode**:

```c
// main.cpp
#define RX_OUTPUT_TEXT 0
```

Port settings: **921600 baud**, 8-N-1. (`RX_SERIAL_BAUD` in the sketch.)

Set `#define RX_OUTPUT_TEXT 1` to output human-readable line insted of machine packets

### Frame

Serial is a byte stream with no inherent boundaries, so each radio packet is
wrapped in a sync-framed envelope:

```
+------+------+-----+-------------------+------+
| 0xAA | 0x55 | len |   payload[len]    | crc8 |
+------+------+-----+-------------------+------+
```

| Field   | Size | Notes                                             |
|---------|------|---------------------------------------------------|
| sync0   | 1    | `0xAA`                                            |
| sync1   | 1    | `0x55`                                            |
| len     | 1    | payload length in bytes (8..76)                   |
| payload | len  | the raw radio packet (see below)                  |
| crc8    | 1    | CRC-8 over `payload` only (not the sync/len bytes)|

**CRC-8:** polynomial `0x07`, init `0x00`, no reflection, no final XOR
(the classic "CRC-8/SMBUS"). Computed over the `len` payload bytes.

The radio link already checks a 16-bit hardware CRC, so a delivered payload is
intact over the air; the crc8 only guards against the USB link mangling bytes.

### Payload (the radio packet)

Little-endian. Fixed 8-byte header, then only the enabled float fields, in
flag-bit order:

```
off 0 : uint8   magic   = 0xA7
off 1 : uint8   flags   (bitmask, see table)
off 2 : uint32  devId   (per-board hardware id — the "MAC")
off 6 : uint16  seq     (per-transmitter counter, wraps at 65535)
off 8 : float   fields...   (present if the matching flag bit is set)
```

`flags` travels in every packet, so one parser handles any transmitter
regardless of how it was compiled.

### Fields (in order)

Fields appear **in ascending bit order**, each present only if its flag bit is
set in `flags`:

| Bit  | Flag    | Name | Floats | Meaning                |
|------|---------|------|--------|------------------------|
| 0x01 | kQuat   | Q    | 4      | quaternion w, x, y, z  |
| 0x02 | kRpy    | RPY  | 3      | roll, pitch, yaw [deg] |
| 0x04 | kAccel  | A    | 3      | accel x, y, z [g]      |
| 0x08 | kGyro   | G    | 3      | gyro x, y, z [deg/s]   |
| 0x10 | kTemp   | T    | 1      | temperature [°C]       |
| 0x20 | kMag    | M    | 3      | mag x, y, z [Gauss]    |

Each float is IEEE-754 32-bit, little-endian (4 bytes). Total payload length =
`8 + 4 * (sum of float counts for set flags)`.
