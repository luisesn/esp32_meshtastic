# TLoRA v1 — Meshtastic Signal & Noise Analyzer

Standalone ESP-IDF firmware for the **TTGO LoRa32 V1.0** (TLoRA v1) board that listens on a Meshtastic channel, decodes incoming packets, samples the LoRa noise floor, and streams all events as **NDJSON** (newline-delimited JSON) over USB serial and **Bluetooth Classic SPP** simultaneously. It also broadcasts a NodeInfo packet every 60 seconds and can transmit **Position packets** on demand.

A self-contained **WebSerial browser UI** (`web/index.html`) can be opened locally or over HTTPS to connect directly to the device, display live packet data with filtering and expandable JSON views, and **send your browser GPS coordinates to the mesh** with one click.

## Hardware

| Component | Detail |
|---|---|
| Board | TTGO LoRa32 V1.0 (LilyGO) |
| MCU | ESP32 dual-core Xtensa LX6, 240 MHz |
| Radio | Semtech SX1276, 868/915 MHz, HF port |
| Display | SSD1306 0.96" OLED, 128×64, I2C |
| USB-serial | CP2102 |
| Crystal | **26 MHz** (not the ESP32 default 40 MHz) |

### Pin Assignments

| Signal | GPIO |
|---|---|
| LoRa SCK | 5 |
| LoRa MISO | 19 |
| LoRa MOSI | 27 |
| LoRa NSS | 18 |
| LoRa RST | 14 |
| LoRa DIO0 | 26 |
| LoRa DIO1 | 33 |
| LoRa DIO2 | 32 |
| OLED SDA | 4 |
| OLED SCL | 15 |
| OLED RST | 16 |

> **Critical:** The OLED on V1.0 uses GPIO 4/15/16. GPIO 21/22 is the header I2C bus and will **not** drive the onboard display.

## Prerequisites

- ESP-IDF **v5.2 LTS** or **v5.3 LTS** — no Arduino layer
- Python 3.8+
- `idf.py` in PATH

Install ESP-IDF by following the [official guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/).

## Build & Flash

```bash
# Set target
idf.py set-target esp32

# Review sdkconfig.defaults — the 26 MHz XTAL setting is critical for V1.0
idf.py menuconfig

# Build
idf.py build

# Flash and open serial monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

The serial monitor will show newline-delimited JSON at 115200 baud.

## Serial Output (NDJSON)

All events are emitted as **NDJSON** (one JSON object per line) on UART0 / USB serial at **115 200 baud**. The same stream is mirrored over **Bluetooth Classic SPP** (device name `TLoRA-Analyzer`).

Use any serial terminal or the included `web/index.html` to consume the stream.

### Decoded RX packet — text message

```json
{"type":"rx","ts":123456,"src":"!8487f01c","dst":"!ffffffff","pkt":3478568197,"hop_limit":3,"hop_start":3,"want_ack":false,"relay_node":0,"rssi":-74,"snr":9,"decoded":true,"portnum":1,"portnum_name":"TEXT_MESSAGE_APP","payload":{"text":"Hello from the mesh!"}}
```

### Decoded RX packet — NodeInfo

```json
{"type":"rx","ts":124000,"src":"!deadbeef","dst":"!ffffffff","pkt":42,"hop_limit":3,"hop_start":3,"want_ack":false,"relay_node":0,"rssi":-94,"snr":3,"decoded":true,"portnum":4,"portnum_name":"NODEINFO_APP","payload":{"id":"!deadbeef","long_name":"TLoRA-beef","short_name":"TLR","hw_model":37,"hw_model_name":"TLORA_V1","macaddr":"aa:bb:cc:dd:ee:ff","licensed":false}}
```

### Decoded RX packet — Position

```json
{"type":"rx","ts":125000,"src":"!deadbeef","dst":"!ffffffff","pkt":43,"hop_limit":3,"hop_start":3,"want_ack":false,"relay_node":0,"rssi":-89,"snr":6,"decoded":true,"portnum":3,"portnum_name":"POSITION_APP","payload":{"lat":37.4219983,"lon":-122.0839998,"alt":15,"gps_time":1718444245}}
```

### Decoded RX packet — Telemetry

```json
{"type":"rx","ts":126000,"src":"!deadbeef","dst":"!ffffffff","pkt":44,"hop_limit":3,"hop_start":3,"want_ack":false,"relay_node":0,"rssi":-91,"snr":5,"decoded":true,"portnum":67,"portnum_name":"TELEMETRY_APP","payload":{"battery_level":85,"voltage":3.92,"channel_utilization":3.2,"air_util_tx":1.1,"temperature":22.5,"relative_humidity":65.0,"barometric_pressure":1013.2}}
```

### Decoded RX packet — relayed (relay_node ≠ 0)

```json
{"type":"rx","ts":128700,"src":"!8487f01c","dst":"!ffffffff","pkt":3478568197,"hop_limit":6,"hop_start":7,"want_ack":false,"relay_node":28,"rssi":-88,"snr":6,"decoded":true,"portnum":1,"portnum_name":"TEXT_MESSAGE_APP","payload":{"text":"hello mesh"}}
```

`hop_start:7, hop_limit:6` means the packet started with hop_limit=7 and has been relayed once. `relay_node:28` is the low byte of the last relaying node's address.

### Undecoded RX packet (foreign channel)

```json
{"type":"rx","ts":12500,"decoded":false,"rssi":-103,"snr":-2,"raw_len":27}
```

### Noise floor sample (every 5 s)

```json
{"type":"noise","ts":5000,"floor_dbm":-121,"samples":32}
```

### TX done

```json
{"type":"tx_done","ts":60000}
```

## WebSerial Browser UI

Open `web/index.html` directly in Chrome or Edge (≥ 89) or serve it over HTTPS.  No installation or build step is required — all logic is self-contained in the single HTML file.

1. Open `web/index.html` in Chrome/Edge
2. Click **Connect** and select the TLoRA serial port (115200 baud)
3. Packets appear in real time; click any row to expand the full JSON

Features: live stats bar, type filter buttons (RX / Foreign / Noise / TX), auto-scroll, Clear button, 500-row rolling window, and a **Send Position** panel — click **Get GPS** to pull coordinates from the browser, then **Send to Mesh** to broadcast a Position packet over LoRa.

## Sending a Position Packet

The firmware accepts JSON commands on UART0 (stdin). The easiest way is via the **WebSerial UI** position panel, but any serial terminal can also send commands manually.

### Via the browser UI

1. Connect WebSerial.
2. In the **Send Position** bar, click **Get GPS** (browser will ask for location permission).
3. Edit the lat/lon/alt fields if needed, then click **Send to Mesh**.

The UI writes one line:

```json
{"cmd":"send_position","lat":52.3702,"lon":4.8952,"alt":5}
```

### Manually (serial terminal)

```bash
# From any terminal connected at 115200 baud
echo '{"cmd":"send_position","lat":52.3702,"lon":4.8952,"alt":5}' > /dev/ttyUSB0
```

The `cmd_task` parses the command, builds a `POSITION_APP` protobuf payload, encrypts it with the configured AES-128-CTR key, and queues it for `lora_tx_task` to broadcast immediately.

## Bluetooth SPP

The device appears as `TLoRA-Analyzer` over Bluetooth Classic. Any SPP-capable app (e.g. Serial Bluetooth Terminal on Android) can connect and receive the same NDJSON stream as the USB serial port.

## Radio Configuration

Fixed — do not change at runtime. All values are in `main/config.h`.

| Parameter | Value |
|---|---|
| Frequency | **869.619 MHz** (derived — see below) |
| Bandwidth | 62.5 kHz |
| Spreading Factor | SF7 |
| Coding Rate | 4/5 |
| Sync Word | `0x2B` |
| Preamble | 16 symbols |
| TX Power | +20 dBm (PA_BOOST) |

### Channel frequency derivation

The TX/RX frequency is not hardcoded. At boot, `sx1276_init()` calls `mesh_channel_freq_hz()` which replicates the Meshtastic firmware algorithm:

```
numSlots   = floor((freqEnd − freqStart) / bw)
           = floor((869.65 − 869.40) / 0.0625) = 4

slot       = djb2(MESH_CHANNEL_NAME) % numSlots
           = djb2("SFNarrow") % 4 = 3

freq       = freqStart + bw/2 + slot × bw
           = 869.400 + 0.03125 + 3 × 0.0625 = 869.61875 MHz
```

Changing `MESH_CHANNEL_NAME` in `config.h` automatically recomputes the correct frequency at next build.

## Meshtastic Channel

Set in `main/config.h`:

```c
#define MESH_CHANNEL_NAME  "SFNarrow"
#define MESH_PSK_INDEX     1           // 1 = AQ== default PSK
```

The frequency, AES-128-CTR key, and channel hash are all **derived at runtime** from these two values — never stored as constants. Changing `MESH_CHANNEL_NAME` or `MESH_PSK_INDEX` is sufficient to switch to a different channel.

**Verified values for `MESH_CHANNEL_NAME="SFNarrow"` and `MESH_PSK_INDEX=1`:**
- Frequency: 869 618 750 Hz
- AES key: `d4f1bb3a20290759f0bcffabcf4e6901`
- Channel hash: `0x20`

## OLED Display

```
┌────────────────────────┐
│ SFNarrow 869.6 SF7     │  ← static config (freq from channel derivation)
│ Noise: -121 dBm        │  ← latest noise floor
│ RX: -89dBm  +7dB       │  ← RSSI / SNR of last packet
│ RX:14    TX:3          │  ← running packet counters
└────────────────────────┘
```

Updated at 1 Hz. The display uses GPIO 16 for hardware reset — this is toggled during `oled_init()` before I2C is initialised.

## Project Structure

```
main/
├── config.h                  all compile-time constants
├── main.c                    app_main, task creation, IRQ handler task
├── sx1276/
│   └── sx1276.h/.c           SPI driver — register R/W, FIFO, signal quality
├── meshtastic/
│   ├── crypto.h/.c           AES-128-CTR, PSK expansion, channel hash
│   ├── packet.h/.c           OTA packet encode/decode
│   └── proto/
│       ├── portnums.h        PortNum enum
│       └── proto_encode.h/.c minimal protobuf wire-format encoder/decoder
├── analyzer/
│   ├── events.h              shared event types, queue/semaphore handles
│   ├── rx_task.c             DIO0 → FIFO read → decode → event queue
│   ├── tx_task.c             NodeInfo broadcast + on-demand TX queue
│   ├── noise_task.c          32-sample noise floor averaging
│   ├── logger_task.c         NDJSON serialiser → UART + Bluetooth SPP
│   └── cmd_task.c            UART JSON command reader (send_position)
├── display/
│   └── oled.h/.c             SSD1306 minimal I2C driver + 5×7 font
└── bt/
    └── bt_spp.h/.c           Bluetooth Classic SPP (Bluedroid, CB mode)
web/
└── index.html                self-contained WebSerial browser UI
```

## Task Architecture

| Task | Stack | Priority | Role |
|---|---|---|---|
| `lora_irq` | 2048 | 5 | Processes DIO0 interrupt, routes to RX/TX handlers |
| `lora_rx` | 4096 | 4 | Reads FIFO, decrypts, decodes, posts event |
| `lora_tx` | 4096 | 3 | 60 s NodeInfo TX + on-demand TX from queue |
| `noise` | 2048 | 2 | 5 s noise floor sampling |
| `logger` | 4096 | 1 | NDJSON serialiser to UART and Bluetooth SPP |
| `display` | 2048 | 1 | 1 Hz OLED update |
| `cmd` | 4096 | 1 | UART JSON command reader; queues on-demand TX |

## Cryptography Notes

- Encryption: **AES-128-CTR** using ESP-IDF's bundled mbedTLS (`esp_mbedtls`)
- The CTR nonce layout matches Meshtastic's `CryptoEngine::encrypt(uint64_t id, uint32_t from)`:
  `[packet_id LE 4B] [0x00 0x00 0x00 0x00] [sender_addr LE 4B] [0x00 0x00 0x00 0x00]`
  (bytes 4–7 are the upper 32 bits of the 64-bit `id` — always zero for normal packets)
- CTR mode uses the same key setup for both encrypt and decrypt
- PSK expansion algorithm is identical to `meshtastic/firmware` `Channels.cpp`

## Known Hardware Gotchas

| Issue | Detail |
|---|---|
| Wrong XTAL | `sdkconfig.defaults` sets `CONFIG_ESP32_XTAL_FREQ=26`; the default of 40 MHz causes UART timing errors |
| Wrong OLED pins | GPIO 21/22 is the **header** I2C; OLED is on GPIO 4/15/16 |
| OLED RST | GPIO 16 must be pulsed LOW→HIGH before I2C init; without this the display ignores all commands |
| SX1276 RST | Must receive LOW pulse (>100 µs) after power-on; done in `sx1276_init()` |
| PA_BOOST only | The SX1276 on this board is wired to PA_BOOST, not RFO — using the RFO path yields no RF output |
| Antenna | Always connect an antenna before powering the board to avoid PA damage at +20 dBm |

## Licence

MIT
