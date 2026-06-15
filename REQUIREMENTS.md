# TLoRA v1 — Meshtastic Signal & Noise Analyzer
## Project Requirements for ESP-IDF

---

## 1. Project Overview

Build a standalone ESP-IDF firmware for the **TLoRA v1** board (ESP32 + SX1276) that:

- Emits valid **Meshtastic v2.x packets** on a custom channel
- Receives and decodes incoming Meshtastic packets from nearby nodes
- Continuously samples the **LoRa noise floor** and RSSI
- Outputs all data as structured **JSON over UART/USB serial**
- Optionally displays live stats on the onboard **OLED display**

The device is no longer supported by official Meshtastic firmware. This project targets the hardware directly using **ESP-IDF v5.x (v5.2 LTS or v5.3 LTS)** — there is no Arduino layer.

---

## 2. Hardware Target

| Component | Detail |
|---|---|
| Board | TTGO LoRa32 V1.0 (LilyGO) |
| MCU | ESP32 (dual-core Xtensa LX6, 240 MHz) |
| Flash | 4 MB |
| XTAL | **26 MHz** (not the ESP32 default of 40 MHz — must be set in `sdkconfig`) |
| Radio | Semtech SX1276 (HF port, 868/915 MHz) |
| Radio interface | SPI (HSPI bus) |
| Display | SSD1306 0.96" OLED (128×64, I2C on dedicated pins) |
| USB-to-serial | CP2102 |
| Battery | LiPo connector + charging circuit onboard |
| Antenna | U.FL/IPEX connector (SMA adapter cable included) |
| Identifier | V1.0 boards have the metal WiFi antenna running along the long edge of the PCB |

> See Annex A for the full pinout diagram and hardware notes.

### SPI Bus — SX1276 (HSPI)

| Signal | GPIO |
|---|---|
| SCK | 5 |
| MISO | 19 |
| MOSI | 27 |
| NSS (CS) | 18 |
| RESET | 14 |
| DIO0 (RxDone/TxDone IRQ) | 26 |
| DIO1 | 33 |
| DIO2 | 32 |

### I2C Bus — SSD1306 OLED (dedicated, non-default pins)

| Signal | GPIO |
|---|---|
| SDA | **4** |
| SCL | **15** |
| RST (software reset) | **16** |
| I2C address | `0x3C` |

> **Critical:** The OLED on the V1.0 uses GPIO 4/15/16, **not** GPIO 21/22. GPIO 21/22 is the general-purpose I2C bus exposed on the headers. Using the wrong pins will result in a silent I2C failure with no error — the display simply won't initialise.
>
> The OLED RST pin (GPIO16) must be toggled LOW→HIGH during boot before calling the SSD1306 init sequence.

---

## 3. Radio Configuration

All radio parameters are fixed. Do not make them runtime-configurable for now.

| Parameter | Value |
|---|---|
| Frequency | 868.0 MHz |
| Bandwidth | 62.5 kHz |
| Spreading Factor | SF7 |
| Coding Rate | 4/5 |
| Sync Word | `0x2B` (Meshtastic standard) |
| Preamble Length | 16 symbols |
| TX Power | +20 dBm (PA_BOOST path, `RegPaDac = 0x87`) |
| CRC | Enabled |
| Implicit Header | Disabled (explicit header mode) |

### SX1276 Register Values to Apply

```
RegFrMsb/Mid/Lsb  : 0xD9, 0x00, 0x00   (868.0 MHz)
RegModemConfig1    : BW=62.5kHz, CR=4/5, explicit header
RegModemConfig2    : SF=7, CRC on
RegModemConfig3    : LNA auto gain, mobile node
RegSyncWord        : 0x2B
RegPreambleMsb/Lsb: 0x00, 0x10          (16 symbols)
RegPaConfig        : 0x8F               (PA_BOOST, max power)
RegPaDac           : 0x87               (+20 dBm mode)
```

---

## 4. Meshtastic Packet Format (v2.x)

### 4.1 Channel Identity

| Field | Value |
|---|---|
| Channel Name | `MESH_CHANNEL_NAME` (compile-time string, default `"SFNarrow"`) |
| PSK Index | `MESH_PSK_INDEX` (compile-time `uint8_t`, default `1` = `AQ==`) |
| Encryption | AES-128-CTR |

Both values come from `config.h`. All crypto material — the expanded AES key and the channel hash — must be **computed at runtime** during `crypto_init()`, not stored as constants. This ensures changing either value in `config.h` is sufficient to reconfigure the firmware with no other edits required.

#### PSK Expansion — `crypto_expand_psk(uint8_t psk_index, uint8_t key_out[16])`

Verified against `meshtastic/firmware` master (`src/mesh/Channels.h` + `Channels.cpp`).

`AQ==` decodes to a single byte `0x01`, which is a PSK *index*, not a raw key. The firmware expands it at runtime:

```c
// The Meshtastic default PSK table (from Channels.h, do not modify)
static const uint8_t MESHTASTIC_DEFAULT_PSK[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

void crypto_expand_psk(uint8_t psk_index, uint8_t key_out[16]) {
    if (psk_index == 0) {
        // Encryption disabled — zero key
        memset(key_out, 0, 16);
        return;
    }
    // Copy the default PSK and bump the last byte by (psk_index - 1)
    memcpy(key_out, MESHTASTIC_DEFAULT_PSK, 16);
    key_out[15] += (psk_index - 1);
    // e.g. psk_index=1 → key_out[15] = 0x01 + 0 = 0x01 (unchanged)
    //      psk_index=2 → key_out[15] = 0x01 + 1 = 0x02
}
```

#### Channel Hash — `crypto_channel_hash(const char *name, const uint8_t key[16])`

The channel hash is a 1-byte value embedded in every outbound packet header and used on RX to quickly filter which channel a packet belongs to. It is derived from the channel name and the expanded key — **not** used in AES at all.

```c
uint8_t crypto_channel_hash(const char *channel_name, const uint8_t key[16]) {
    uint8_t h = 0;
    // XOR all bytes of the channel name
    for (size_t i = 0; channel_name[i] != '\0'; i++)
        h ^= (uint8_t)channel_name[i];
    // XOR all bytes of the expanded AES key
    for (size_t i = 0; i < 16; i++)
        h ^= key[i];
    return h;
}
// Example: "SFNarrow" + defaultpsk(index=1) → hash = 0x20
```

#### Initialisation sequence in `crypto_init()`

```c
void crypto_init(void) {
    // 1. Expand PSK index → raw AES key
    crypto_expand_psk(MESH_PSK_INDEX, g_aes_key);

    // 2. Derive channel hash from name + key
    g_channel_hash = crypto_channel_hash(MESH_CHANNEL_NAME, g_aes_key);

    // 3. Initialise mbedTLS AES context with the derived key
    mbedtls_aes_init(&g_aes_ctx);
    // (set key just before each encrypt/decrypt call, not here,
    //  since CTR mode requires direction-agnostic key setup)
}
```

Both `g_aes_key` and `g_channel_hash` are module-private globals in `crypto.c`, exposed only via accessor functions.

### 4.2 Over-the-Air Packet Layout

```
Bytes 0–3   : Destination node address (uint32_t, little-endian)
Bytes 4–7   : Sender node address (uint32_t, little-endian)
Bytes 8–11  : Packet ID (uint32_t, little-endian)
Byte  12    : Flags (want_ack, hop_limit packed)
Bytes 13+   : AES-128-CTR encrypted Protobuf payload (MeshPacket.decoded)
```

### 4.3 Node Address

Derive the self node address from the ESP32 MAC address:
```c
uint32_t node_addr = (mac[2] << 24) | (mac[3] << 16) | (mac[4] << 8) | mac[5];
```

### 4.4 Protobuf Encoding

Use **nanopb** to encode/decode `MeshPacket` and sub-messages.

Source the `.proto` files from: `https://github.com/meshtastic/protobufs` (pin to a recent stable tag).

Generate nanopb `.pb.c` / `.pb.h` files from:
- `meshtastic/mesh.proto`
- `meshtastic/telemetry.proto`
- `meshtastic/portnums.proto`

For TX, the minimum viable packet is a **NodeInfo** broadcast:
- `portnum = NODEINFO_APP`
- `want_response = false`
- Fill `User` message with a short ID string derived from node address

### 4.5 AES-128-CTR Nonce Construction

Meshtastic CTR nonce (16 bytes):
```
Bytes 0–3   : Packet ID (little-endian)
Bytes 4–7   : Sender node address (little-endian)
Bytes 8–15  : 0x00 padding
```

Use **mbedTLS** (`esp_mbedtls` component, included in ESP-IDF) for AES.

---

## 5. Firmware Architecture

### 5.1 RTOS Tasks (FreeRTOS)

| Task | Stack | Priority | Role |
|---|---|---|---|
| `sx1276_irq_handler` | 2048 | 5 (high) | Processes DIO0 GPIO interrupt, posts event to queue |
| `lora_rx_task` | 4096 | 4 | Reads FIFO on RxDone event, decodes packet, posts to logger |
| `lora_tx_task` | 4096 | 3 | Sends NodeInfo broadcast on a fixed interval (e.g. 60s) |
| `noise_sampler_task` | 2048 | 2 | Samples RSSI register periodically when radio is idle |
| `logger_task` | 4096 | 1 (low) | Serializes all events to JSON, writes to UART |
| `display_task` | 2048 | 1 (low) | Updates OLED with latest stats (optional) |

### 5.2 Inter-task Communication

Use a single **FreeRTOS queue** (`event_queue`, depth 16) carrying tagged event structs:

```c
typedef enum {
    EVT_RX_PACKET,
    EVT_TX_DONE,
    EVT_NOISE_SAMPLE,
    EVT_ERROR,
} event_type_t;

typedef struct {
    event_type_t type;
    union {
        rx_packet_t  rx;
        noise_data_t noise;
    };
} analyzer_event_t;
```

### 5.3 SX1276 Driver

Write a minimal SPI driver — no Arduino abstraction layer. Must implement:

- `sx1276_init()` — SPI init, chip identity check (reg `0x42` == `0x12`), full register config
- `sx1276_set_mode(mode)` — SLEEP, STDBY, RX_CONTINUOUS, TX
- `sx1276_write_fifo(buf, len)` — load TX FIFO
- `sx1276_read_fifo(buf, len)` — drain RX FIFO
- `sx1276_read_reg(addr)` / `sx1276_write_reg(addr, val)`
- `sx1276_get_rssi()` — returns `int16_t` dBm: `−157 + RegRssiValue`
- `sx1276_get_snr()` — returns `int8_t` dB: `RegPktSnrValue / 4`
- `sx1276_get_noise_floor()` — average of N=32 RSSI samples taken in RX mode with no preamble detected

DIO0 must be wired to a GPIO interrupt (rising edge) that posts to the event queue.

---

## 6. Noise Floor Sampling

- Radio must be in **RX Continuous** mode during sampling
- Check `RegModemStat` bit 0 (`signal_detected`) before each sample — only sample when no signal is being received
- Take **32 consecutive samples** at ~1 ms intervals
- Average and report as `noise_floor_dbm` (int16_t)
- Sample every **5 seconds** when TX is not in progress
- RSSI formula: `dBm = −157 + RegRssiValue` (SX1276 HF port, >779 MHz)

---

## 7. JSON Output Format (UART)

All output is newline-delimited JSON on UART0 at **115200 baud**. Three event types:

### RX Packet (decoded)
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
  "payload_summary": "NodeInfo: id=!deadbeef"
}
```

### RX Packet (undecoded / foreign channel)
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

### Noise Sample
```json
{
  "event": "noise_sample",
  "ts_ms": 125000,
  "noise_floor_dbm": -121,
  "sample_count": 32
}
```

---

## 8. OLED Display (Optional)

Use the `esp_lvgl_port` component or a minimal `ssd1306` I2C driver.

Display layout (128×64):

```
┌────────────────────────┐
│ SFNarrow  868.0MHz SF7 │  ← static config line
│ Noise: -121 dBm        │  ← latest noise floor
│ Last RX: -89dBm  +7dB  │  ← RSSI / SNR of last packet
│ RX: 14  TX: 3          │  ← running packet counters
└────────────────────────┘
```

Update rate: 1 Hz maximum to avoid I2C contention with radio SPI.

---

## 9. ESP-IDF Component Dependencies

All components should be managed via **IDF Component Manager** (`idf_component.yml`) where possible.

| Component | Source | Purpose |
|---|---|---|
| `esp_driver_spi` | IDF built-in | SX1276 SPI bus |
| `esp_driver_gpio` | IDF built-in | DIO0 interrupt, RST |
| `esp_driver_i2c` | IDF built-in | OLED |
| `mbedtls` | IDF built-in | AES-128-CTR |
| `nanopb` | idf-component-registry or vendored | Protobuf encode/decode |
| `esp_timer` | IDF built-in | Timestamps |
| `freertos` | IDF built-in | Tasks, queues |

Do **not** use `arduino-esp32` or any Arduino compatibility layer.

---

## 10. Project Structure

```
tlora-analyzer/
├── CMakeLists.txt
├── sdkconfig.defaults
├── idf_component.yml
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                  ← app_main, task spawning
│   ├── sx1276/
│   │   ├── sx1276.h
│   │   └── sx1276.c            ← SPI driver
│   ├── meshtastic/
│   │   ├── crypto.h / .c       ← AES-128-CTR + key derivation
│   │   ├── packet.h / .c       ← encode / decode MeshPacket
│   │   └── proto/              ← generated nanopb files
│   ├── analyzer/
│   │   ├── rx_task.c
│   │   ├── tx_task.c
│   │   ├── noise_task.c
│   │   └── logger_task.c
│   ├── display/
│   │   └── oled.h / .c         ← SSD1306 minimal driver
│   └── config.h                ← all constants (pins, radio params, key)
└── README.md
```

---

## 11. `config.h` — Compile-time Parameters

This file is the **single place** to change any configuration. The implementation must not scatter magic numbers elsewhere. Crypto material (AES key bytes, channel hash) must never appear here — they are derived at runtime by `crypto_init()` from `MESH_CHANNEL_NAME` and `MESH_PSK_INDEX`.

```c
// --- SPI (HSPI) — SX1276 ---
#define LORA_SPI_HOST       HSPI_HOST
#define LORA_SPI_FREQ_HZ    10000000    // 10 MHz
#define LORA_SPI_SCK        5
#define LORA_SPI_MISO       19
#define LORA_SPI_MOSI       27
#define LORA_SPI_NSS        18
#define LORA_PIN_RST        14
#define LORA_PIN_DIO0       26          // RxDone / TxDone IRQ
#define LORA_PIN_DIO1       33
#define LORA_PIN_DIO2       32

// --- I2C — SSD1306 OLED (V1.0 dedicated pins, NOT GPIO21/22) ---
#define OLED_SDA            4
#define OLED_SCL            15
#define OLED_RST            16          // Must be toggled LOW→HIGH on boot
#define OLED_ADDR           0x3C
#define OLED_WIDTH          128
#define OLED_HEIGHT         64

// --- Misc board hardware ---
#define LED_BUILTIN         2           // Blue LED (active HIGH)
#define BTN_BUILTIN         0           // Boot/user button (active LOW)

// --- Radio PHY ---
#define LORA_FREQ_HZ        868000000UL
#define LORA_BW_KHZ         62.5f       // RegModemConfig1 BW bits = 0b110
#define LORA_SF             7
#define LORA_CR             5           // 4/5
#define LORA_SYNC_WORD      0x2B
#define LORA_PREAMBLE_LEN   16
#define LORA_TX_POWER_DBM   20          // PA_BOOST + RegPaDac=0x87

// --- Meshtastic channel (only these two need changing to switch channel) ---
#define MESH_CHANNEL_NAME   "SFNarrow"  // Used for hash derivation and display
#define MESH_PSK_INDEX      1           // 0=no encryption, 1=AQ== default, 2+=bumped key

// --- TX behaviour ---
#define MESH_TX_INTERVAL_MS 60000

// --- Noise sampler ---
#define NOISE_SAMPLE_COUNT  32
#define NOISE_INTERVAL_MS   5000

// --- UART ---
#define UART_BAUD           115200
```

---

## 12. Build & Flash

```bash
# Set target
idf.py set-target esp32

# Configure (review sdkconfig.defaults first)
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

`sdkconfig.defaults` must enable:
- `CONFIG_ESP32_XTAL_FREQ=26` ← **critical for V1.0, board will malfunction with default 40 MHz**
- `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`
- `CONFIG_MBEDTLS_AES_C=y`
- `CONFIG_MBEDTLS_CTR_DRBG_C=y`
- `CONFIG_LOG_DEFAULT_LEVEL_INFO=y`

---

## 13. Implementation Order

Build and validate in this sequence to keep each step testable in isolation:

1. **SX1276 SPI driver** — init, chip ID read, register read/write
2. **Radio config** — apply all PHY registers, verify with loopback or SDR
3. **Noise sampler** — RX continuous + RSSI polling, JSON output
4. **RX path** — DIO0 IRQ → FIFO read → raw hex JSON output
5. **Crypto layer** — implement `crypto_expand_psk()` + `crypto_channel_hash()` + `crypto_init()`; unit-test by asserting that `MESH_CHANNEL_NAME="SFNarrow"` + `MESH_PSK_INDEX=1` produces key `d4f1bb3a20290759f0bcffabcf4e6901` and hash `0x20`; then test CTR decrypt against a captured known packet
6. **Protobuf layer** — nanopb decode of decrypted payload
7. **TX path** — NodeInfo packet construction, encode, encrypt, transmit
8. **OLED display** — last step, purely cosmetic

---

## 14. Out of Scope (for now)

- Runtime configuration (all params are compile-time constants in `config.h`)
- Full 256-bit PSK support (only the 16-byte index-based expansion is implemented)
- Multiple simultaneous channel support
- Regulatory TX power compliance
- OTA firmware update
- Web interface or BLE companion app
- Spectrum sweep across frequencies

---

## Annex A — TTGO LoRa32 V1.0 Hardware Reference

### Board Identification

The V1.0 can be distinguished from later revisions by the **metal WiFi antenna trace running along the long edge of the PCB** (later V2.x boards moved the antenna to the short end or replaced it with an SMA connector). The PCB silkscreen reads `ESP32-PICO-D4` or simply `TTGO`.

### Full GPIO Map

```
                    TTGO LoRa32 V1.0
                  ┌──────────────────┐
            GND  ─┤ GND        GND  ├─ GND
            3V3  ─┤ 3V3        GND  ├─ GND
             36  ─┤ IO36       RST  ├─ RESET (EN)
             37  ─┤ IO37       IO34 ├─ 34  (input only)
             38  ─┤ IO38       IO35 ├─ 35  (input only)
             39  ─┤ IO39       IO32 ├─ 32  ← LORA DIO2
  LORA DIO0  26  ─┤ IO26       IO33 ├─ 33  ← LORA DIO1
             25  ─┤ IO25       IO25 ├─ 25
             17  ─┤ IO17       IO12 ├─ 12
             16  ─┤ IO16  ──── ─   ├─     ← OLED RST (internal)
   OLED SCL  15  ─┤ IO15       IO13 ├─ 13
   OLED SDA   4  ─┤ IO4        IO14 ├─ 14  ← LORA RST
             TX   ─┤ TX (IO1)  IO27 ├─ 27  ← LORA MOSI
             RX   ─┤ RX (IO3)  IO26 ├─     (see IO26 above)
              2  ─┤ IO2 (LED) IO25  ├─     (see IO25 above)
              0  ─┤ IO0 (BTN) IO19  ├─ 19  ← LORA MISO
              5  ─┤ IO5        IO18 ├─ 18  ← LORA NSS/CS
   LORA SCK   5  ─┤ (same)    IO23 ├─ 23
             21  ─┤ IO21 (SDA) IO22├─ 22  (general I2C SCL)
            GND  ─┤ GND        GND ├─ GND
            3V3  ─┤ 3V3        5V  ├─ 5V
                  └──────────────────┘
```

> This is a schematic approximation. Consult the [LilyGO TTGO-LORA32 GitHub repo](https://github.com/LilyGO/TTGO-LORA32) for the original schematic PDF.

### Internal Connections (not on headers)

These signals are wired PCB-internally between the ESP32 and the SX1276/SSD1306. They are **not accessible on the pin headers** and must only be referenced as GPIO numbers in firmware:

| Function | GPIO | Direction | Notes |
|---|---|---|---|
| LORA SCK | 5 | Out | HSPI clock |
| LORA MISO | 19 | In | HSPI MISO |
| LORA MOSI | 27 | Out | HSPI MOSI |
| LORA NSS | 18 | Out | SPI chip select, active LOW |
| LORA RST | 14 | Out | Active LOW reset pulse |
| LORA DIO0 | 26 | In | RxDone / TxDone interrupt |
| LORA DIO1 | 33 | In | RxTimeout / FhssChangeChannel |
| LORA DIO2 | 32 | In | FhssChangeChannel (unused here) |
| OLED SDA | 4 | Bidir | Dedicated I2C data for SSD1306 |
| OLED SCL | 15 | Out | Dedicated I2C clock for SSD1306 |
| OLED RST | 16 | Out | Active LOW, must be toggled on boot |
| LED (blue) | 2 | Out | Active HIGH |
| Button | 0 | In | Active LOW, also BOOT pin |

### XTAL Frequency — Critical

The TTGO LoRa32 V1.0 uses a **26 MHz crystal oscillator**, not the ESP32 default of 40 MHz. Failing to set this in `sdkconfig` causes incorrect baud rates on UART, incorrect SPI timing, and the board may appear to function but will exhibit subtle timing bugs.

Set in `sdkconfig.defaults`:
```
CONFIG_ESP32_XTAL_FREQ=26
```

Or via `idf.py menuconfig`:
```
Component config → ESP32-specific → Main XTAL frequency → 26 MHz
```

The quartz crystal is located on the back of the board between pins 19 and the SRT pad. It is marked `26.000` and is small enough to require a magnifying glass to read.

### SPI Bus Assignment

The SX1276 must use the **HSPI** peripheral (`HSPI_HOST` / `SPI2_HOST` in ESP-IDF v5.x). Do not use VSPI — its default pins (GPIO 18/19/23) conflict with the SX1276 wiring on this board.

In ESP-IDF v5.x:
```c
spi_bus_config_t buscfg = {
    .mosi_io_num   = LORA_SPI_MOSI,   // 27
    .miso_io_num   = LORA_SPI_MISO,   // 19
    .sclk_io_num   = LORA_SPI_SCK,    // 5
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
};
spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
```

### OLED Initialisation Sequence (V1.0 specific)

The V1.0 OLED requires a **manual hardware reset via GPIO16** before the SSD1306 will respond on I2C. Without this the display will be unresponsive even if I2C is wired correctly:

```c
// Must run before i2c_master_init() or any SSD1306 command
gpio_set_direction(OLED_RST, GPIO_MODE_OUTPUT);
gpio_set_level(OLED_RST, 0);
vTaskDelay(pdMS_TO_TICKS(20));
gpio_set_level(OLED_RST, 1);
vTaskDelay(pdMS_TO_TICKS(5));
// Now safe to initialise the I2C bus and SSD1306 driver
```

### Antenna

The SX1276 RF output is routed to a **U.FL/IPEX connector**. An SMA adapter cable is usually included in the box. **Always connect an antenna before powering the board** — transmitting into an open RF port will damage the SX1276 PA over time, especially at +20 dBm.

For 868 MHz a quarter-wave whip is ~8.2 cm. The included antenna is adequate for bench work.

### Power Supply

| Source | Voltage | Notes |
|---|---|---|
| USB (CP2102) | 5V via USB | Normal development/monitoring |
| LiPo battery | 3.7V nominal | JST 1.25mm connector, onboard charger |
| 3V3 pin | 3.3V regulated | Max ~500 mA from onboard LDO |
| 5V pin | 5V (USB passthrough) | Only present when USB connected |

### Known Gotchas

| Issue | Detail |
|---|---|
| Wrong XTAL | Default ESP-IDF assumes 40 MHz — always override to 26 MHz for this board |
| Wrong OLED pins | GPIO 21/22 is the *header* I2C bus; OLED uses GPIO 4/15/16 internally |
| No RST on SX1276 without pulse | After power-on the SX1276 must receive a RST LOW pulse (≥100 µs) before register access |
| DIO1/DIO2 not critical for basic use | DIO0 alone is sufficient for RxDone/TxDone; DIO1 and DIO2 are reserved for future timeout/FHSS use |
| PA_BOOST path only | The SX1276 on this board is wired to PA_BOOST, not RFO. Setting `RegPaConfig` with the RFO bit will produce no RF output |
