# Changelog

All notable changes to the TLoRA v1 Meshtastic Signal & Noise Analyzer firmware.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [Phase 13] — Position TX + BT Discoverability Fixes — 2026-06-15

### Added

- **Position packet TX** — the device can now broadcast a `POSITION_APP` packet to the mesh on demand:
  - `proto_encode_position()` in `proto_encode.c`: encodes a `Position` protobuf using `sfixed32` (field tag `WT_FIX32`) for `latitude_i` / `longitude_i` and signed varint for `altitude`; new helpers `write_sfixed32()` and `write_int32()` added alongside the existing `write_uint32()`
  - `packet_build_position(node_addr, lat_i, lon_i, alt_m, out_buf)` in `packet.c`: wraps the position proto, encrypts with AES-128-CTR, and assembles the full 16-byte OTA header + ciphertext into a ready-to-transmit buffer
  - `lora_tx_pkt_t` struct (255-byte data + 1-byte length) and `g_tx_request_queue` (depth 4) added to `events.h`

- **On-demand TX queue** — `tx_task.c` rewritten with a `do_tx()` helper and `xQueueReceive()` dispatch:
  - `lora_tx_task` blocks on `g_tx_request_queue` with a timeout equal to the remaining NodeInfo interval
  - On-demand packets (e.g. position) are transmitted immediately when received; the NodeInfo timer fires when the queue times out — no polling, no busy-wait

- **`cmd_task.c`** (new file) — reads newline-delimited JSON commands from UART0 stdin:
  - `json_get_double()`: minimal key-value extractor using `strstr` + `strtod`; no dynamic allocation
  - `handle_send_position()`: converts `lat`/`lon`/`alt` doubles to `int32_t` scaled values (`×1e7` for lat/lon), calls `packet_build_position()`, posts `lora_tx_pkt_t` to `g_tx_request_queue`
  - Registered in `CMakeLists.txt` (SRCS) and created in `app_main()` with 4096-byte stack, priority 1

- **UART VFS driver** in `main.c` — `uart_vfs_dev_register()` + `uart_driver_install(UART_NUM_0, …)` + `uart_vfs_dev_use_driver(0)` enables `fgets(stdin)` in `cmd_task` alongside existing `printf(stdout)` on the same UART; `esp_driver_uart` added to `CMakeLists.txt` REQUIRES

- **WebSerial position panel** in `web/index.html`:
  - **Get GPS** button: calls `navigator.geolocation.getCurrentPosition()` with `enableHighAccuracy: true`; pre-fills lat/lon/alt inputs and shows accuracy in the status line
  - **Lat / Lon / Alt** numeric inputs: editable after GPS fill or manually
  - **Send to Mesh** button: serialises `{"cmd":"send_position","lat":…,"lon":…,"alt":…}` and writes it via `port.writable.getWriter()` / `writer.releaseLock()` pattern
  - All controls disabled when serial is disconnected; `updatePosPanel()` called on connect and disconnect

### Fixed

- **Bluetooth device not discoverable** — `esp_bt_gap_set_device_name()` and `esp_bt_gap_set_scan_mode()` were being called before `esp_spp_enhanced_init()` returned. Per the Bluedroid SPP server example, these calls must happen inside the `ESP_SPP_START_EVT` callback (after the SPP server has started). The device name is now stored in `s_device_name[32]` so the async callback can access it.

- **`esp_bt_controller_enable` returning `ESP_ERR_INVALID_ARG`** — the existing `sdkconfig` had `CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y` (the Kconfig default), which initialises the controller in BLE mode. Calling `esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)` then mismatched the configured mode (checked at line 1919 of `bt.c`). Fixed by adding `CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=y` to `sdkconfig.defaults` and deleting the stale generated `sdkconfig` so it regenerates correctly.

- **NVS not initialised before BT** — the Bluedroid stack silently fails if `nvs_flash_init()` has not been called before `esp_bt_controller_init()`. Added NVS init (with erase-on-corruption fallback) to `app_main()` before `bt_spp_init()`.

---

## [Phase 12] — NDJSON Serial Output + WebSerial UI + Bluetooth SPP — 2026-06-15

### Added

- **NDJSON serial output** — `logger_task.c` now emits one JSON object per line on UART (115 200 baud).
  Every field that was visible in the old human-readable output is preserved:
  - `{"type":"rx","ts":...,"src":"!xxxxxxxx","dst":"!xxxxxxxx","pkt":...,"hop_limit":...,"hop_start":...,"want_ack":...,"relay_node":...,"rssi":...,"snr":...,"decoded":true,"portnum":...,"portnum_name":"...","payload":{...}}`
  - `{"type":"rx","ts":...,"decoded":false,"rssi":...,"snr":...,"raw_len":...}` (foreign channel)
  - `{"type":"noise","ts":...,"floor_dbm":...,"samples":...}`
  - `{"type":"tx_done","ts":...}`
  - Per-portnum `payload` objects: `TEXT_MESSAGE_APP`→`{"text":"..."}`, `NODEINFO_APP`→id/long_name/short_name/hw_model/macaddr/licensed, `POSITION_APP`→lat/lon/alt/gps_time, `ROUTING_APP`→error_code/error_name, `TELEMETRY_APP`→battery/voltage/channel_utilization/air_util_tx and/or temp/humidity/pressure, unknown→`{"hex":"...","len":N}`
  - Text and string fields are JSON-escaped (`json_esc()` handles `"`, `\`, control characters)

- **WebSerial browser UI** — `web/index.html` (self-contained, no external dependencies):
  - Works locally or over HTTPS; requires Chrome/Edge ≥ 89
  - Connect/disconnect via WebSerial (115 200 baud)
  - Live stats bar: RX count, foreign count, noise samples, TX count, noise floor, last RSSI/SNR
  - Filter buttons: All / RX / Foreign / Noise / TX
  - Packet log with timestamp, type badge, and one-line summary per packet
  - Click any row to expand the pretty-printed raw JSON
  - Auto-scroll toggle; Clear button; capped at 500 rows to prevent memory growth

- **Bluetooth Classic SPP** — `main/bt/bt_spp.h` / `main/bt/bt_spp.c`:
  - Device name: `TLoRA-Analyzer` (connectable and generally discoverable)
  - Uses Bluedroid / `ESP_SPP_MODE_CB`; releases BLE RAM via `esp_bt_controller_mem_release()`
  - `bt_spp_write(data, len)` called by `output_line()` alongside `printf()` — every JSON record sent on both UART and the BT SPP channel simultaneously
  - Congestion-aware: writes are skipped if client signals congestion; no-op when no client connected
  - Non-fatal: if BT init fails at boot, UART output continues unaffected
  - Guarded by `CONFIG_BT_ENABLED`; stubs in header so BT can be disabled without changing callers
  - `sdkconfig.defaults` additions: `CONFIG_BT_ENABLED`, `CONFIG_BT_BLUEDROID_ENABLED`, `CONFIG_BT_CLASSIC_ENABLED`, `CONFIG_BT_SPP_ENABLED`
  - `CMakeLists.txt`: added `bt/bt_spp.c` to SRCS, `bt` to REQUIRES, `bt` dir to INCLUDE_DIRS

### Changed
- `logger_task.c` rewritten: replaced human-readable sectioned output with NDJSON
  - OLED display helpers (`build_disp_summary`, `update_disp_pkt`) retained unchanged
  - `output_line()` replaces all direct `printf` calls; routes to both UART and BT SPP

---

## [Phase 11] — Meshtastic 2.5+ Compatibility + AES Nonce Fix — 2026-06-15

### Fixed

- **AES-128-CTR nonce layout was wrong** — all packet decryption was silently failing since initial implementation.
  - Meshtastic's `CryptoEngine::encrypt(uint64_t id, uint32_t from, …)` takes a 64-bit `id`, so the nonce layout is:
    `[packet_id LE 4B] [0x00 0x00 0x00 0x00] [sender_addr LE 4B] [0x00 0x00 0x00 0x00]`
  - Our code incorrectly placed `sender_addr` at bytes 4–7 (the zero-pad slot) instead of bytes 8–11.
    This produced a completely different AES keystream and garbage decrypted output on every received packet.

- **OTA header length mismatch for Meshtastic 2.5+**
  - Meshtastic 2.3 added a `channel` byte to the PacketHeader (13 → 14 bytes).
  - Meshtastic 2.5 added `next_hop` and `relay_node` bytes (14 → 16 bytes).
  - Our `MESH_HEADER_LEN` was hardcoded to 13 — we were passing up to 3 header bytes into AES-CTR
    as payload, corrupting both the keystream offset and the proto parse input.
  - Updated `MESH_HEADER_LEN` to **16** and expanded `mesh_header_t` accordingly.

- **`proto_decode_data` returned false on valid unknown wire types** (wire type 1 = int64, wire type 5 = fixed32).
  - These are valid protobuf types that newer Meshtastic firmware may include in the `Data` message
    (e.g. `bitfield` field 9).  The decoder now skips them with the correct byte-skip instead of failing.

### Added
- `channel`, `next_hop`, `relay_node` fields in `mesh_header_t` (packed, matches Meshtastic 2.5+ OTA struct)
- `relay_node` field in `mesh_packet_t`, `rx_packet_t` (event struct)
- Logger now prints `via !xxNN` on the packet header line when `relay_node ≠ 0`
- `ESP_LOG_BUFFER_HEX` of raw received bytes and decrypted payload in `rx_task.c` (INFO level) — aids field debugging
- `packet_build_nodeinfo()` now fills `channel = crypto_get_channel_hash()` so our TX packets include the channel byte expected by 2.5+ receivers

---

## [Phase 10] — Human-Readable Serial Output + Full Portnum Parsing — 2026-06-15

### Added
- `proto_decode_position()` in `proto_encode.c`: decodes `Position` protobuf
  - `sfixed32` wire-type (fixed32) for `latitude_i` / `longitude_i` — requires new `read_fixed32()` helper
  - `altitude` (int32 varint, metres HAE), `time` (uint32 varint, Unix timestamp from GPS)
- `proto_decode_routing()`: decodes `Routing` protobuf
  - Extracts `error_reason` (field 3, RouteError enum) — covers ACK, NO_ROUTE, TIMEOUT, etc.
- `proto_decode_telemetry()`: decodes `Telemetry` protobuf
  - Parses `DeviceMetrics` sub-message (field 2): battery %, voltage, ch-util %, air-tx %
  - Parses `EnvironmentMetrics` sub-message (field 3): temperature, humidity, barometric pressure
  - Both sub-messages use `float` = fixed32 wire type
- `mesh_position_t`, `mesh_routing_t`, `mesh_telemetry_t` structs in `proto_encode.h`
- `WT_FIX32` wire-type constant and `read_fixed32()` decode helper in `proto_encode.c`

### Changed
- `logger_task.c` rewritten: replaced newline-delimited JSON with human-readable sectioned output
  - Each RX packet printed as a labelled block with a header line (src/dst/pkt/hops), RF line, and portnum-specific body
  - `TEXT_MESSAGE_APP`: prints raw UTF-8 payload as a quoted string
  - `POSITION_APP`: prints decimal degrees with N/S/E/W, altitude, and GPS UTC date-time via `gmtime_r()`
  - `NODEINFO_APP`: prints long name, node ID, short name, hardware model name, and MAC address
  - `ROUTING_APP`: prints named error reason string
  - `TELEMETRY_APP`: prints device metrics and/or environment metrics depending on which sub-messages are present
  - Foreign channel (undecoded) packets printed with `[FOREIGN CHANNEL]` label and raw byte count
  - Noise and TX-done events condensed to single-line format
- `rx_task.c`: inner portnum payload bytes now passed through the event via `raw_payload`/`raw_len` (decoded=true)
  — all per-portnum decode logic moved to `logger_task`; summary string no longer built in `rx_task`

---

## [Phase 9] — Meshtastic Channel Frequency Derivation — 2026-06-15

### Added
- `main/meshtastic/channel_freq.c` / `channel_freq.h`: `mesh_channel_freq_hz()`
  - Implements the Meshtastic firmware frequency-slot algorithm: DJB2 hash of channel name → channel slot → centre frequency
  - Formula: `freq = freqStart + bw/2 + (djb2(name) % numSlots) × bw`
  - For `SFNarrow` + EU_868 (869.4–869.65 MHz, BW=62.5 kHz): 4 slots, slot 3 → **869 618 750 Hz**
- `LORA_REGION_FREQ_START_HZ` / `LORA_REGION_FREQ_END_HZ` in `config.h` (EU_868 band limits)

### Changed
- `sx1276_init()`: frequency registers (`REG_FR_MSB/MID/LSB`) now computed at runtime from `mesh_channel_freq_hz()` instead of hardcoded `0xD90000` (868.0 MHz)
  - Frf formula: `(uint64_t)freq_hz × 2¹⁹ / 32 000 000`
  - Boot log now prints the derived frequency and channel name
- `LORA_FREQ_HZ` in `config.h` updated to `869618750UL` (reference value; actual register is always computed)
- OLED config line updated from `868.0` to `869.6` in `main.c`

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
  - `crypto_ctr()`: in-place AES-128-CTR using `mbedtls_aes_crypt_ctr()`; nonce = `[packet_id LE 4B][zeros 4B][sender_addr LE 4B][zeros 4B]` (corrected in Phase 11)
  - `crypto_get_channel_hash()`: returns the pre-computed channel hash
- Verified values for `MESH_CHANNEL_NAME="SFNarrow"` + `MESH_PSK_INDEX=1`:
  - AES key: `d4f1bb3a20290759f0bcffabcf4e6901`
  - Channel hash: `0x20`
- All crypto material derived at runtime — no raw key bytes in `config.h`

---

## [Phase 4] — RX Path — 2026-06-15

### Added
- `main/meshtastic/packet.c` / `packet.h`: OTA packet header encode/decode
  - 13-byte header (original): dst_addr, src_addr, packet_id, flags — expanded to 16 bytes in Phase 11
  - `packet_decode()`: validates header, decrypts payload in-place
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
  - Frequency: `RegFr = 0xD90000` → 868.0 MHz (later replaced by runtime derivation in Phase 9)
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
