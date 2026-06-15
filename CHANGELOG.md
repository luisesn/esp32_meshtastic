# Changelog

All notable changes to the TLoRA v1 Meshtastic Signal & Noise Analyzer firmware.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Phase 8] — Display Layer — 2026-06-15

### Added
- `main/display/oled.c` / `oled.h`: minimal SSD1306 I2C driver
  - Hardware reset via GPIO 16 (`OLED_RST`) before I2C initialisation — required for V1.0 boards
  - 1024-byte frame buffer (`s_fb[8][128]`) for page-based rendering
  - Embedded 5×7 ASCII font (0x20–0x7E, 95 glyphs × 5 bytes)
  - `oled_clear()`, `oled_puts(col, row, str)`, `oled_flush()` API
  - Uses legacy I2C driver (`driver/i2c.h`) compatible with ESP-IDF v5.2+
- `display_task` in `main.c`:
  - 1 Hz update rate (avoids I2C contention with SPI radio traffic)
  - Shows: channel/freq/SF config, noise floor, last RX RSSI/SNR, RX/TX counters
  - Reads from `g_stats` via mutex — no dependency on the event queue

### Notes
- OLED is non-fatal: if `oled_init()` fails (display not present), a warning is logged and the firmware continues normally
- GPIO 4/15/16 are the dedicated OLED pins on V1.0; GPIO 21/22 is the header I2C bus

---

## [Phase 7] — TX Path — 2026-06-15

### Added
- `main/analyzer/tx_task.c`: NodeInfo broadcast task
  - Sends every `MESH_TX_INTERVAL_MS` (60 s default)
  - Constructs `User` → `Data` protobuf chain, encrypts with AES-128-CTR, writes to SX1276 FIFO
  - Waits for `g_tx_done_sem` (given by IRQ handler on TxDone), falls back to 5 s timeout
  - Posts `EVT_TX_DONE` to `g_event_queue` after each transmission
  - Returns to RX continuous mode after TX completes
- `packet_build_nodeinfo()` in `meshtastic/packet.c`:
  - Node ID string: `!<hex addr>` (e.g. `!deadbeef`)
  - Long name: `TLoRA-<lower 16 bits hex>`, short name: `<lower 16 bits hex>`
  - Hardware model set to `37` (TLORA_V1)
  - Broadcasts to `0xFFFFFFFF`, hop_limit=3
- SX1276 `REG_DIO_MAPPING1` switched to `0x40` (DIO0=TxDone) before TX, restored to `0x00` (DIO0=RxDone) after
- LED blink on TX in `tx_task.c` (GPIO 2, active HIGH)

---

## [Phase 6] — Protobuf Layer — 2026-06-15

### Added
- `main/meshtastic/proto/proto_encode.c` / `proto_encode.h`: minimal protobuf wire-format encoder/decoder
  - No external code generator required; directly implements protobuf binary wire format
  - `proto_encode_user()`: encodes `User` message (fields: id, long_name, short_name, macaddr, hw_model, is_licensed)
  - `proto_encode_data()`: encodes `Data` message (fields: portnum, payload, want_response)
  - `proto_decode_data()`: decodes `Data` from raw bytes with graceful handling of unknown fields
  - `proto_decode_user()`: decodes `User` from raw bytes
  - Wire-format compatible with nanopb-generated code from `meshtastic/protobufs`
- `main/meshtastic/proto/portnums.h`: `meshtastic_PortNum` enum and `portnum_name()` helper
- `mesh_user_t` and `mesh_data_t` structs in `proto_encode.h`

### Notes
- To replace with nanopb-generated files: generate `.pb.c`/`.pb.h` from `meshtastic/mesh.proto`,
  `meshtastic/telemetry.proto`, and `meshtastic/portnums.proto` using `nanopb_generator.py`, then
  swap `proto_encode.c` for the generated code and add the nanopb library to `idf_component.yml`

---

## [Phase 5] — Crypto Layer — 2026-06-15

### Added
- `main/meshtastic/crypto.c` / `crypto.h`: AES-128-CTR encryption/decryption
  - `crypto_init()`: expands PSK index → raw 16-byte AES key; derives channel hash; inits mbedTLS context
  - `crypto_expand_psk()`: implements the Meshtastic default PSK expansion algorithm (from `Channels.cpp`)
  - `crypto_channel_hash()`: XOR-folds channel name bytes and key bytes into a 1-byte hash
  - `crypto_ctr()`: in-place AES-128-CTR using `mbedtls_aes_crypt_ctr()`; nonce = `[packet_id LE][sender_addr LE][zeros]`
  - `crypto_get_channel_hash()`: returns the pre-computed channel hash
- Verified values for `MESH_CHANNEL_NAME="SFNarrow"` + `MESH_PSK_INDEX=1`:
  - AES key: `d4f1bb3a20290759f0bcffabcf4e6901`
  - Channel hash: `0x20`
- All crypto material derived at runtime — no raw key bytes in `config.h`

---

## [Phase 4] — RX Path — 2026-06-15

### Added
- `main/meshtastic/packet.c` / `packet.h`: OTA packet header encode/decode
  - 13-byte header: dst_addr, src_addr, packet_id, flags (hop_limit + want_ack + channel hash nibble)
  - `packet_decode()`: validates header, applies channel hash filter (upper nibble of flags), decrypts payload in-place
  - `packet_build_nodeinfo()`: builds a full TX packet ready to write to the SX1276 FIFO
- `main/analyzer/rx_task.c`: `lora_rx_task` (priority 4)
  - Waits on `g_rx_ready_queue` signalled by the IRQ handler task
  - Reads `REG_FIFO_RX_CURR_ADDR`, sets `REG_FIFO_ADDR_PTR`, reads `REG_RX_NB_BYTES`, reads FIFO
  - Attempts packet decode; on success parses inner `Data` protobuf and populates `payload_summary`
  - Posts `EVT_RX_PACKET` to `g_event_queue`; updates `g_stats` via mutex
- `main/analyzer/logger_task.c`: `logger_task` (priority 1)
  - Consumes from `g_event_queue`; serialises all event types to newline-delimited JSON via `printf`
- DIO0 GPIO ISR: `vTaskNotifyGiveFromISR` → `sx1276_irq_handler_task`
  - IRQ handler reads `REG_IRQ_FLAGS`, routes `RxDone` to `g_rx_ready_queue` and `TxDone` to `g_tx_done_sem`
  - Discards packets with `IRQ_PAYLOAD_CRC_ERROR`

---

## [Phase 3] — Noise Sampler — 2026-06-15

### Added
- `main/analyzer/noise_task.c`: `noise_sampler_task` (priority 2)
  - Samples `REG_RSSI_VALUE` up to `NOISE_SAMPLE_COUNT` (32) times at ~1 ms intervals
  - Skips samples when `REG_MODEM_STAT` bit 0 (`signal_detected`) is set
  - Averages valid samples; falls back to −120 dBm if all samples are skipped
  - Posts `EVT_NOISE_SAMPLE` to `g_event_queue` every `NOISE_INTERVAL_MS` (5 s)
  - Updates `g_stats.last_noise_dbm` via mutex
- `sx1276_get_noise_floor()` in `sx1276.c`: wraps the sampling loop; called by noise_task in RX continuous mode
- `sx1276_get_rssi()`: returns `−157 + RegPktRssiValue` (SX1276 HF port formula)
- `sx1276_get_snr()`: returns `RegPktSnrValue / 4` dB

---

## [Phase 2] — Radio Configuration — 2026-06-15

### Added
- Full PHY register configuration in `sx1276_init()`:
  - Frequency: `RegFr = 0xD90000` → 868.0 MHz
  - `RegModemConfig1 = 0x62` → BW=62.5 kHz, CR=4/5, explicit header
  - `RegModemConfig2 = 0x74` → SF7, CRC enabled
  - `RegModemConfig3 = 0x04` → AGC auto-on
  - `RegPreamble = 0x0010` → 16 symbols
  - `RegSyncWord = 0x2B` → Meshtastic sync word
  - `RegPaConfig = 0x8F` → PA_BOOST, OutputPower=15
  - `RegPaDac = 0x87` → +20 dBm high-power mode
  - `RegLna = 0x23` → max gain + HF boost
- `sx1276_set_rx_mode()`: sets `RegDioMapping1=0x00` (DIO0=RxDone), enters RX continuous
- `sx1276_set_tx_mode()`: sets `RegDioMapping1=0x40` (DIO0=TxDone), enters TX
- `sdkconfig.defaults`:
  - `CONFIG_ESP32_XTAL_FREQ=26` — critical for V1.0 (26 MHz crystal, not 40 MHz)
  - `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`
  - `CONFIG_MBEDTLS_AES_C=y`, `CONFIG_MBEDTLS_CTR_DRBG_C=y`
  - `CONFIG_LOG_DEFAULT_LEVEL_INFO=y`

---

## [Phase 1] — SX1276 SPI Driver — 2026-06-15

### Added
- `main/sx1276/sx1276.c` / `sx1276.h`: minimal SPI driver for SX1276
  - `sx1276_init()`: configures RST GPIO, initialises HSPI (`SPI2_HOST`) bus at 10 MHz, verifies chip version (`REG_VERSION == 0x12`)
  - `sx1276_read_reg()` / `sx1276_write_reg()`: single-byte register R/W via 16-bit SPI transaction
  - `sx1276_read_fifo()` / `sx1276_write_fifo()`: burst FIFO transfers up to 255 bytes
  - `sx1276_set_mode()`: writes `RegOpMode` (SLEEP=0x80, STDBY=0x81, TX=0x83, RX_CONTINUOUS=0x85)
  - SPI2_HOST (HSPI) with SCK=5, MISO=19, MOSI=27, NSS=18 — no conflict with VSPI defaults
  - SX1276 RST pulse (LOW 10 ms → HIGH 10 ms) on init as required by datasheet
- `main/config.h`: all compile-time constants (pins, radio PHY, Meshtastic channel, timing)
- `main/CMakeLists.txt`: component registration with all source files and include directories
- `main/analyzer/events.h`: shared event types (`analyzer_event_t`, `rx_packet_t`, `noise_data_t`), queue/semaphore extern declarations, `analyzer_stats_t`
- `main/main.c`: `app_main()` — MAC-to-node-address derivation, crypto init, SX1276 init, OLED init, FreeRTOS object creation, GPIO ISR install, task creation
- `README.md`: full hardware reference, build instructions, JSON output format, architecture overview
