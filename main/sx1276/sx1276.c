#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "sx1276.h"
#include "../config.h"

static const char *TAG = "sx1276";
static spi_device_handle_t s_spi = NULL;

/* ── Register access ────────────────────────────────────────────────────── */

uint8_t sx1276_read_reg(uint8_t addr) {
    spi_transaction_t t = {
        .flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 16,
        .tx_data = {addr & 0x7F, 0x00},
    };
    spi_device_transmit(s_spi, &t);
    return t.rx_data[1];
}

void sx1276_write_reg(uint8_t addr, uint8_t val) {
    spi_transaction_t t = {
        .flags  = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 16,
        .tx_data = {addr | 0x80, val},
    };
    spi_device_transmit(s_spi, &t);
}

void sx1276_write_fifo(const uint8_t *buf, uint8_t len) {
    /* Extra byte for the address prefix — SX1276 FIFO can hold 256 bytes */
    uint8_t tx_buf[257];
    tx_buf[0] = REG_FIFO | 0x80;
    memcpy(tx_buf + 1, buf, len);
    spi_transaction_t t = {
        .length    = (len + 1) * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = NULL,
    };
    spi_device_transmit(s_spi, &t);
}

void sx1276_read_fifo(uint8_t *buf, uint8_t len) {
    uint8_t tx_buf[257];
    uint8_t rx_buf[257];
    memset(tx_buf, 0, len + 1);
    tx_buf[0] = REG_FIFO & 0x7F;
    spi_transaction_t t = {
        .length    = (len + 1) * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };
    spi_device_transmit(s_spi, &t);
    memcpy(buf, rx_buf + 1, len);
}

void sx1276_set_mode(uint8_t mode) {
    sx1276_write_reg(REG_OP_MODE, mode);
}

/* ── Signal quality ─────────────────────────────────────────────────────── */

int16_t sx1276_get_rssi(void) {
    /* SX1276 HF port (>779 MHz): RSSI = -157 + RegPktRssiValue */
    return -157 + (int16_t)sx1276_read_reg(REG_PKT_RSSI_VALUE);
}

int8_t sx1276_get_snr(void) {
    /* RegPktSnrValue is a signed byte in units of 0.25 dB */
    return (int8_t)((int8_t)sx1276_read_reg(REG_PKT_SNR_VALUE)) / 4;
}

int16_t sx1276_get_noise_floor(void) {
    /* Average RegRssiValue over up to NOISE_SAMPLE_COUNT samples,
     * skipping any sample where a signal is being received (ModemStat bit0). */
    int32_t sum   = 0;
    int     count = 0;
    for (int i = 0; i < NOISE_SAMPLE_COUNT; i++) {
        uint8_t modem_stat = sx1276_read_reg(REG_MODEM_STAT);
        if (!(modem_stat & 0x01)) {
            sum += -157 + (int16_t)sx1276_read_reg(REG_RSSI_VALUE);
            count++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return (count > 0) ? (int16_t)(sum / count) : -120;
}

/* ── Mode helpers ────────────────────────────────────────────────────────── */

void sx1276_set_rx_mode(void) {
    /* DIO0 = RxDone (mapping 00) */
    sx1276_write_reg(REG_DIO_MAPPING1, 0x00);
    sx1276_set_mode(MODE_RX_CONTINUOUS);
}

void sx1276_set_tx_mode(void) {
    /* DIO0 = TxDone (mapping 01) */
    sx1276_write_reg(REG_DIO_MAPPING1, 0x40);
    sx1276_set_mode(MODE_TX);
}

/* ── Initialisation ──────────────────────────────────────────────────────── */

static void sx1276_reset(void) {
    gpio_set_level(LORA_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t sx1276_init(void) {
    /* RST pin as output */
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LORA_PIN_RST,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    sx1276_reset();

    /* SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num   = LORA_SPI_MOSI,
        .miso_io_num   = LORA_SPI_MISO,
        .sclk_io_num   = LORA_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LORA_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LORA_SPI_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = LORA_SPI_NSS,
        .queue_size     = 8,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LORA_SPI_HOST, &devcfg, &s_spi));

    /* Verify chip identity */
    uint8_t version = sx1276_read_reg(REG_VERSION);
    if (version != SX1276_VERSION) {
        ESP_LOGE(TAG, "SX1276 not found (version=0x%02X, expected 0x12)", version);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SX1276 detected, version=0x%02X", version);

    /* Switch to LoRa sleep mode first (required before changing LoRa/FSK bit) */
    sx1276_write_reg(REG_OP_MODE, 0x00);   /* FSK sleep */
    vTaskDelay(pdMS_TO_TICKS(10));
    sx1276_write_reg(REG_OP_MODE, 0x80);   /* LoRa sleep */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Frequency: 868.0 MHz
     * Frf = 868e6 * 2^19 / 32e6 = 14221312 = 0xD90000 */
    sx1276_write_reg(REG_FR_MSB, 0xD9);
    sx1276_write_reg(REG_FR_MID, 0x00);
    sx1276_write_reg(REG_FR_LSB, 0x00);

    /* ModemConfig1: BW=62.5kHz (0110), CR=4/5 (001), explicit header (0)
     * = 0110_0010 = 0x62 */
    sx1276_write_reg(REG_MODEM_CONFIG1, 0x62);

    /* ModemConfig2: SF=7 (0111), TxCont=0, CRC=1, SymbTimeout=0
     * = 0111_0100 = 0x74 */
    sx1276_write_reg(REG_MODEM_CONFIG2, 0x74);

    /* ModemConfig3: LowDataRateOptimize=0, AgcAutoOn=1 */
    sx1276_write_reg(REG_MODEM_CONFIG3, 0x04);

    /* Preamble: 16 symbols */
    sx1276_write_reg(REG_PREAMBLE_MSB, 0x00);
    sx1276_write_reg(REG_PREAMBLE_LSB, 0x10);

    /* Sync word: 0x2B (Meshtastic standard) */
    sx1276_write_reg(REG_SYNC_WORD, LORA_SYNC_WORD);

    /* PA: PA_BOOST path, OutputPower=15 → 0x8F
     * RegPaDac=0x87 enables +20 dBm mode on PA_BOOST */
    sx1276_write_reg(REG_PA_CONFIG, 0x8F);
    sx1276_write_reg(REG_PA_DAC,    0x87);

    /* LNA: maximum gain + HF boost */
    sx1276_write_reg(REG_LNA, 0x23);

    /* FIFO base addresses */
    sx1276_write_reg(REG_FIFO_TX_BASE_ADDR, 0x00);
    sx1276_write_reg(REG_FIFO_RX_BASE_ADDR, 0x00);

    /* DIO0 pin as input with pull-down, rising edge interrupt */
    gpio_config_t dio_conf = {
        .pin_bit_mask = 1ULL << LORA_PIN_DIO0,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_POSEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&dio_conf));

    /* Leave in standby until tasks are ready */
    sx1276_set_mode(MODE_STDBY);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "SX1276 configured: 868.0 MHz, BW=62.5kHz, SF7, CR=4/5, sync=0x%02X, +20dBm",
             LORA_SYNC_WORD);
    return ESP_OK;
}
