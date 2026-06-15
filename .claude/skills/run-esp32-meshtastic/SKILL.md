---
name: run-esp32-meshtastic
description: build, verify, flash, or monitor the esp32_meshtastic firmware; run, start, build, compile, flash, serial monitor
---

ESP-IDF firmware for the TTGO LoRa32 V1.0 (TLoRA v1) — a Meshtastic packet analyzer/broadcaster.
The project cross-compiles for ESP32; there is no way to execute it on the host.
The build driver is `smoke.sh` in this directory; it builds and validates the binary.
Flashing and serial monitoring require a physical board on `/dev/ttyUSB0`.

## Prerequisites

ESP-IDF is already sourced in this devcontainer (`idf.py` is in PATH, IDF_PATH is set).
No additional install step is needed.

Verify with:
```bash
idf.py --version
# Expected: ESP-IDF v6.1-dev-... or similar
```

## Build (agent path)

```bash
bash .claude/skills/run-esp32-meshtastic/smoke.sh
```

Exits 0 on success. Prints memory layout and flash offsets. Takes ~5 s on an incremental build (longer on a cold build).

Expected tail output:
```
=== Build OK ===
  esp32_meshtastic.bin  206 KB
│ Flash Code  │  87834 │ ...
│ IRAM        │  58331 │  44.5 ...
│ Flash Data  │  53764 │ ...
│ DRAM        │  15362 │   8.5 ...
```

To rebuild from scratch:
```bash
idf.py fullclean && idf.py build
```

## Build (human path)

```bash
idf.py build          # incremental
idf.py size           # show memory breakdown
```

## Flash + monitor (hardware required)

Connect TTGO LoRa32 V1.0 via USB. A CP2102 appears as `/dev/ttyUSB0`.

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

The serial monitor outputs structured lines at 115 200 baud.
Press `Ctrl-]` to exit the monitor.

### Expected serial output

Decoded RX packet:
```
--- RX  00:01:23.456  ----------------------------------------
    src: !a1b2c3d4    dst: BROADCAST    pkt: 0x00001234  hops: 3
    RF:  RSSI -87 dBm  SNR +8 dB
    ---
    TEXT_MESSAGE: "Hello from the mesh!"
------------------------------------------------------------------------
```

NodeInfo:
```
--- RX  00:01:24.100  ----------------------------------------
    src: !deadbeef    dst: BROADCAST    pkt: 0x00001235  hops: 2
    RF:  RSSI -94 dBm  SNR +3 dB
    ---
    NODEINFO: "TLoRA-beef" (!deadbeef)  short: "TLR"
              hw: TLORA_V1        MAC: AA:BB:CC:DD:EE:FF
------------------------------------------------------------------------
```

Position:
```
--- RX  00:01:25.200  ----------------------------------------
    src: !deadbeef    dst: BROADCAST    pkt: 0x00001236  hops: 3
    RF:  RSSI -89 dBm  SNR +6 dB
    ---
    POSITION: 37.4219983 N  122.0839998 W  alt: 15 m
              GPS: 2024-06-15 10:30:45 UTC
------------------------------------------------------------------------
```

Telemetry:
```
    TELEMETRY (device): batt 85%  3.92 V  ch-util 3.2%  air-tx 1.1%
    TELEMETRY (env):    temp 22.5 C  humidity 65.0%  pressure 1013.2 hPa
```

Noise floor (every 5 s):
```
--- noise  00:00:05.000   floor: -112 dBm  (32 samples)
--- TX done  00:01:00.000
```

Foreign channel packet (not on SFNarrow):
```
--- RX  00:00:12.500  [FOREIGN CHANNEL]  ----------------------------------
    RF: RSSI -103 dBm  SNR -2 dB   raw: 27 bytes
------------------------------------------------------------------------
```

## Key config knobs (`main/config.h`)

| Macro | Current value | Effect |
|---|---|---|
| `MESH_CHANNEL_NAME` | `"SFNarrow"` | Channel name; frequency + AES key both derived from this |
| `MESH_PSK_INDEX` | `1` | `1` = default `AQ==` PSK |
| `LORA_REGION_FREQ_START_HZ` | `869400000` | EU_868 band start |
| `LORA_REGION_FREQ_END_HZ` | `869650000` | EU_868 band end |
| `LORA_TX_POWER_DBM` | `20` | PA_BOOST, always use an antenna |
| `MESH_TX_INTERVAL_MS` | `60000` | NodeInfo broadcast interval |

Changing `MESH_CHANNEL_NAME` is sufficient to switch channel — frequency, AES key, and channel hash all re-derive at boot.

## Gotchas

- **26 MHz XTAL** — `sdkconfig.defaults` sets `CONFIG_ESP32_XTAL_FREQ=26`. The board ships with a 26 MHz crystal; leaving it at the ESP-IDF default of 40 MHz causes UART timing errors and garbled serial output. Do not `idf.py fullclean` without restoring this.
- **PA_BOOST only** — the SX1276 is wired to the PA_BOOST path. Using the RFO path (power < 14 dBm without the flag) produces no RF output. Always connect an antenna before powering on.
- **OLED on GPIO 4/15/16** — the V1.0 board's onboard SSD1306 is on these pins, not the header I2C bus (21/22). GPIO 16 must be pulsed LOW→HIGH before I2C init or the display ignores all commands.
- **`idf.py monitor` baud** — must be 115200 (set in `config.h` as `UART_BAUD`). Other baud rates produce garbage.
- **Frequency is derived, not stored** — `LORA_FREQ_HZ` in `config.h` is a reference comment. The actual SX1276 register is written by `mesh_channel_freq_hz()` at boot using DJB2(channel_name) % numSlots. For `SFNarrow` + EU_868 this yields **869 618 750 Hz**.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `ninja: error: ... no rule to make target` | Run `idf.py set-target esp32` then rebuild |
| UART output is garbage | Wrong XTAL setting — verify `CONFIG_ESP32_XTAL_FREQ=26` in `sdkconfig` |
| OLED blank on boot | GPIO 16 reset not pulsing — check `oled_init()` in `display/oled.c` |
| `idf.py flash` fails with "No serial ports found" | CP2102 driver missing — `sudo apt-get install cp210x-gpio` or use `lsusb` to confirm device |
| Build: `fatal error: channel_freq.h` | Run `idf.py build` from repo root, not from `main/` |
