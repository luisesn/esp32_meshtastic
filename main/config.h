#pragma once

/* ── SPI (HSPI/SPI2) — SX1276 ──────────────────────────────────────────── */
#define LORA_SPI_HOST       SPI2_HOST
#define LORA_SPI_FREQ_HZ    10000000        /* 10 MHz */
#define LORA_SPI_SCK        5
#define LORA_SPI_MISO       19
#define LORA_SPI_MOSI       27
#define LORA_SPI_NSS        18
#define LORA_PIN_RST        14
#define LORA_PIN_DIO0       26              /* RxDone / TxDone IRQ */
#define LORA_PIN_DIO1       33
#define LORA_PIN_DIO2       32

/* ── I2C — SSD1306 OLED (V1.0 dedicated pins, NOT GPIO 21/22) ────────── */
#define OLED_SDA            4
#define OLED_SCL            15
#define OLED_RST            16              /* Must be toggled LOW→HIGH on boot */
#define OLED_ADDR           0x3C
#define OLED_WIDTH          128
#define OLED_HEIGHT         64

/* ── Misc board hardware ────────────────────────────────────────────────── */
#define LED_BUILTIN         2               /* Blue LED, active HIGH */
#define BTN_BUILTIN         0               /* Boot/user button, active LOW */

/* ── EU_868 region band (Meshtastic frequency slot source) ──────────────── */
#define LORA_REGION_FREQ_START_HZ   869400000UL   /* 869.400 MHz */
#define LORA_REGION_FREQ_END_HZ     869650000UL   /* 869.650 MHz */

/* ── Radio PHY ──────────────────────────────────────────────────────────── */
#define LORA_BW_KHZ         62.5f           /* RegModemConfig1 BW bits = 0b0110 */
#define LORA_SF             7
#define LORA_CR             5               /* 4/5 */
#define LORA_SYNC_WORD      0x2B
#define LORA_PREAMBLE_LEN   16
#define LORA_TX_POWER_DBM   20              /* PA_BOOST + RegPaDac=0x87 */

/* ── Meshtastic channel (only these two need changing to switch channel) ── */
#define MESH_CHANNEL_NAME   "SFNarrow"      /* Used for hash derivation and display */
#define MESH_PSK_INDEX      1               /* 0=no encryption, 1=AQ== default */

/* ── TX behaviour ───────────────────────────────────────────────────────── */
#define MESH_TX_INTERVAL_MS 60000

/* ── Noise sampler ──────────────────────────────────────────────────────── */
#define NOISE_SAMPLE_COUNT  32
#define NOISE_INTERVAL_MS   5000

/* ── UART ───────────────────────────────────────────────────────────────── */
#define UART_BAUD           115200
