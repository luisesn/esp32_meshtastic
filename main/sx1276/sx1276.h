#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ── SX1276 LoRa register addresses ────────────────────────────────────── */
#define REG_FIFO                0x00
#define REG_OP_MODE             0x01
#define REG_FR_MSB              0x06
#define REG_FR_MID              0x07
#define REG_FR_LSB              0x08
#define REG_PA_CONFIG           0x09
#define REG_OCP                 0x0B
#define REG_LNA                 0x0C
#define REG_FIFO_ADDR_PTR       0x0D
#define REG_FIFO_TX_BASE_ADDR   0x0E
#define REG_FIFO_RX_BASE_ADDR   0x0F
#define REG_FIFO_RX_CURR_ADDR   0x10
#define REG_IRQ_FLAGS_MASK      0x11
#define REG_IRQ_FLAGS           0x12
#define REG_RX_NB_BYTES         0x13
#define REG_MODEM_STAT          0x18
#define REG_PKT_SNR_VALUE       0x19
#define REG_PKT_RSSI_VALUE      0x1A
#define REG_RSSI_VALUE          0x1B
#define REG_MODEM_CONFIG1       0x1D
#define REG_MODEM_CONFIG2       0x1E
#define REG_SYMB_TIMEOUT_LSB    0x1F
#define REG_PREAMBLE_MSB        0x20
#define REG_PREAMBLE_LSB        0x21
#define REG_PAYLOAD_LENGTH      0x22
#define REG_MODEM_CONFIG3       0x26
#define REG_DETECT_OPTIMIZE     0x31
#define REG_DETECTION_THRESHOLD 0x37
#define REG_SYNC_WORD           0x39
#define REG_DIO_MAPPING1        0x40
#define REG_DIO_MAPPING2        0x41
#define REG_VERSION             0x42
#define REG_PA_DAC              0x4D

/* ── IRQ flag bits ──────────────────────────────────────────────────────── */
#define IRQ_RX_TIMEOUT          0x80
#define IRQ_RX_DONE             0x40
#define IRQ_PAYLOAD_CRC_ERROR   0x20
#define IRQ_VALID_HEADER        0x10
#define IRQ_TX_DONE             0x08
#define IRQ_CAD_DONE            0x04
#define IRQ_FHSS_CHANGE_CHANNEL 0x02
#define IRQ_CAD_DETECTED        0x01

/* ── Operating modes (LoRa mode, bit7=1) ───────────────────────────────── */
#define MODE_SLEEP              0x80
#define MODE_STDBY              0x81
#define MODE_TX                 0x83
#define MODE_RX_CONTINUOUS      0x85

#define SX1276_VERSION          0x12

esp_err_t sx1276_init(void);
void      sx1276_set_mode(uint8_t mode);
uint8_t   sx1276_read_reg(uint8_t addr);
void      sx1276_write_reg(uint8_t addr, uint8_t val);
void      sx1276_write_fifo(const uint8_t *buf, uint8_t len);
void      sx1276_read_fifo(uint8_t *buf, uint8_t len);
int16_t   sx1276_get_rssi(void);
int8_t    sx1276_get_snr(void);
int16_t   sx1276_get_noise_floor(void);
void      sx1276_set_tx_mode(void);
void      sx1276_set_rx_mode(void);
