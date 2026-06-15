# TLoRA v1 — Meshtastic Signal & Noise Analyzer

Standalone ESP-IDF firmware for the **TTGO LoRa32 V1.0** (TLoRA v1) board that listens on a Meshtastic channel, decodes incoming packets, samples the LoRa noise floor, and reports all events as structured JSON over USB serial. It also broadcasts a NodeInfo packet every 60 seconds, making the device visible to nearby Meshtastic nodes.

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

## JSON Output

All events are printed as newline-delimited JSON on UART0 (USB serial).

### Decoded RX packet (own channel)

```json
{
  "event": "rx_packet",
  "ts_ms": 123456,
  "rssi_dbm": -89,
  "snr_db": 7,
  "src_addr": "0xDEADBEEF",
  "dst_addr": "0xFFFFFFFF",
  "packet_id": 42,
  "portnum": "NODEINFO_APP",
  "decoded": true,
  "payload_summary": "NodeInfo: id=!deadbeef name=TLoRA-beef"
}
```

### Undecoded RX packet (foreign channel or unknown format)

```json
{
  "event": "rx_packet",
  "ts_ms": 123457,
  "rssi_dbm": -102,
  "snr_db": 3,
  "src_addr": "unknown",
  "decoded": false,
  "raw_len": 27
}
```

### Noise floor sample

```json
{
  "event": "noise_sample",
  "ts_ms": 125000,
  "noise_floor_dbm": -121,
  "sample_count": 32
}
```

## Radio Configuration

Fixed — do not change at runtime. All values are in `main/config.h`.

| Parameter | Value |
|---|---|
| Frequency | 868.0 MHz |
| Bandwidth | 62.5 kHz |
| Spreading Factor | SF7 |
| Coding Rate | 4/5 |
| Sync Word | `0x2B` |
| Preamble | 16 symbols |
| TX Power | +20 dBm (PA_BOOST) |

## Meshtastic Channel

Set in `main/config.h`:

```c
#define MESH_CHANNEL_NAME  "SFNarrow"
#define MESH_PSK_INDEX     1           // 1 = AQ== default PSK
```

The AES-128-CTR key and channel hash are **derived at runtime** by `crypto_init()` from these two values — never stored as constants. Changing `MESH_CHANNEL_NAME` or `MESH_PSK_INDEX` is sufficient to switch to a different channel.

**Verified values for `MESH_CHANNEL_NAME="SFNarrow"` and `MESH_PSK_INDEX=1`:**
- AES key: `d4f1bb3a20290759f0bcffabcf4e6901`
- Channel hash: `0x20`

## OLED Display

```
┌────────────────────────┐
│ SFNarrow 868.0 SF7     │  ← static config
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
│   ├── sx1276.h/.c           SPI driver — register R/W, FIFO, signal quality
├── meshtastic/
│   ├── crypto.h/.c           AES-128-CTR, PSK expansion, channel hash
│   ├── packet.h/.c           OTA packet encode/decode
│   └── proto/
│       ├── portnums.h        PortNum enum
│       └── proto_encode.h/.c minimal protobuf wire-format encoder/decoder
├── analyzer/
│   ├── events.h              shared event types, queue/semaphore handles
│   ├── rx_task.c             DIO0 → FIFO read → decode → event queue
│   ├── tx_task.c             60 s NodeInfo broadcast
│   ├── noise_task.c          32-sample noise floor averaging
│   └── logger_task.c         JSON serialisation to UART
└── display/
    └── oled.h/.c             SSD1306 minimal I2C driver + 5×7 font
```

## Task Architecture

| Task | Stack | Priority | Role |
|---|---|---|---|
| `lora_irq` | 2048 | 5 | Processes DIO0 interrupt, routes to RX/TX handlers |
| `lora_rx` | 4096 | 4 | Reads FIFO, decrypts, decodes, posts event |
| `lora_tx` | 4096 | 3 | 60 s NodeInfo TX |
| `noise` | 2048 | 2 | 5 s noise floor sampling |
| `logger` | 4096 | 1 | JSON serialisation |
| `display` | 2048 | 1 | 1 Hz OLED update |

## Cryptography Notes

- Encryption: **AES-128-CTR** using ESP-IDF's bundled mbedTLS (`esp_mbedtls`)
- The CTR nonce is `[packet_id LE 4B][sender_addr LE 4B][zeros 8B]`
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
