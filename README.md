# ESP32 Wireless Flasher for STM32

ESP32-S3 based wireless STM32F1 firmware flasher. The device creates a Wi-Fi access point, serves a web UI, accepts firmware uploads, stores firmware packages in LittleFS, and flashes STM32F103 targets through SWD.

## Features

- ESP32-S3 Wi-Fi AP with embedded web interface
- STM32F1 flashing over SWD
- Default wiring uses only SWDIO, SWCLK, and GND; NRST is not required
- Intel HEX upload with automatic address parsing and binary conversion
- Legacy `manifest.json` + `app.bin` upload support
- Firmware package validation with size and CRC32 checks
- Saved firmware package list stored in LittleFS
- Persistent flash firmware selection after reboot
- LittleFS storage usage and free-space display in the web UI
- Flash progress, status log, timing log, and cancel support
- STM32 chip information readout over SWD

## Hardware wiring

| ESP32-S3 | STM32F103 |
| --- | --- |
| GND | GND |
| GPIO11 | SWDIO |
| GPIO12 | SWCLK |

Do not connect NRST for the default workflow.

## Web workflow

1. Power on the ESP32-S3 flasher.
2. Connect to the ESP32 access point.
3. Open the web UI shown by the device.
4. Upload an Intel HEX firmware file, or upload `manifest.json` and `app.bin` together.
5. Optionally save the validated firmware to the saved package list.
6. Select the firmware to flash in the **Start Flashing** section.
7. Click start flashing and wait for program and verify to finish.

## Firmware package formats

### Intel HEX

Recommended format. HEX files contain address records and checksums, so the flasher can infer the target flash address and generate the internal binary package automatically.

### manifest.json + app.bin

Legacy format. The manifest describes the target chip, address, size, and CRC32 for the raw binary.

Example manifest:

```json
{
  "target": "stm32f103",
  "address": 134217728,
  "size": 21028,
  "crc32": 1234567890
}
```

## Build

This project uses PlatformIO.

```bash
platformio run
```

Target environment:

- Platform: `espressif32 @ 6.5.0`
- Board: `exlink_esp32s3_16mb`
- Framework: Arduino
- Filesystem: LittleFS
- Partition table: `my.csv`

## Upload to ESP32-S3

```bash
platformio run --target upload
```

Default upload speed is 460800 baud.

## Release firmware flashing offsets

The release package includes the compiled ESP32-S3 firmware and required boot files. Flash them with these offsets:

| Offset | File |
| --- | --- |
| `0x0000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xE000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin` |

Example:

```bash
esptool.py --chip esp32s3 --baud 460800 write_flash \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0xE000 boot_app0.bin \
  0x10000 firmware.bin
```

## Repository layout

- `src/flash/` - STM32 flashing backends and flash manager
- `src/hal/` - SWD transport and target control
- `src/storage/` - LittleFS firmware package storage
- `src/web/` - embedded web server, HTML, and JavaScript
- `src/network/` - Wi-Fi AP setup
- `src/display/` - optional display status output
- `data/` - static web UI mirror
- `tools/` - firmware manifest helper scripts
- `boards/` - custom ESP32-S3 board definition

## Notes

This project is intended for STM32F103 targets using SWD. The current stable default transport is SWD on GPIO11/GPIO12 without reset-line control.
