#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "config.h"
#include "sx1276/sx1276.h"
#include "meshtastic/crypto.h"
#include "meshtastic/packet.h"
#include "analyzer/events.h"
#include "display/oled.h"

static const char *TAG = "main";

/* ── Global handles (declared extern in events.h) ────────────────────────── */

QueueHandle_t    g_event_queue     = NULL;
QueueHandle_t    g_rx_ready_queue  = NULL;
SemaphoreHandle_t g_tx_done_sem    = NULL;
TaskHandle_t     g_irq_task_handle = NULL;
analyzer_stats_t g_stats           = {0};
SemaphoreHandle_t g_stats_mutex    = NULL;
uint32_t         g_node_addr       = 0;
uint8_t          g_node_mac[6]     = {0};

/* ── Task forward declarations ──────────────────────────────────────────── */
void lora_rx_task(void *pvParameters);
void lora_tx_task(void *pvParameters);
void noise_sampler_task(void *pvParameters);
void logger_task(void *pvParameters);

/* ── DIO0 GPIO ISR ───────────────────────────────────────────────────────── */

static void IRAM_ATTR dio0_isr_handler(void *arg) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(g_irq_task_handle, &woken);
    if (woken) portYIELD_FROM_ISR();
}

/* ── IRQ handler task (priority 5) ──────────────────────────────────────── */

static void sx1276_irq_handler_task(void *pvParameters) {
    ESP_LOGI(TAG, "IRQ handler task started");
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t irq_flags = sx1276_read_reg(REG_IRQ_FLAGS);
        sx1276_write_reg(REG_IRQ_FLAGS, 0xFF);   /* clear all flags */

        if (irq_flags & IRQ_RX_DONE) {
            if (irq_flags & IRQ_PAYLOAD_CRC_ERROR) {
                ESP_LOGW(TAG, "RxDone with CRC error — discarding");
            } else {
                uint8_t sig = 1;
                xQueueSendFromISR(g_rx_ready_queue, &sig, NULL);
            }
        }

        if (irq_flags & IRQ_TX_DONE) {
            xSemaphoreGiveFromISR(g_tx_done_sem, NULL);
        }
    }
}

/* ── Display task (priority 1) ──────────────────────────────────────────── */

static void display_task(void *pvParameters) {
    char line[48];   /* larger than worst-case format output; oled_puts clips at OLED_WIDTH */
    ESP_LOGI(TAG, "Display task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        analyzer_stats_t s;
        if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            s = g_stats;
            xSemaphoreGive(g_stats_mutex);
        } else {
            continue;
        }

        oled_clear();

        /* Row 0: channel / freq / modem config (static) */
        snprintf(line, sizeof(line), "%s 869.6 SF7", MESH_CHANNEL_NAME);
        oled_puts(0, 0, line);

        /* Row 1: noise floor + packet counters */
        snprintf(line, sizeof(line), "Nf:%4d RX:%-4" PRIu32 "TX:%-3" PRIu32,
                 s.last_noise_dbm, s.rx_count, s.tx_count);
        oled_puts(0, 1, line);

        /* Rows 2-3: most recent decoded packet */
        if (s.disp_pkts[0].valid) {
            snprintf(line, sizeof(line), "!%08" PRIx32 " %4d %+3ddB",
                     s.disp_pkts[0].src_addr,
                     s.disp_pkts[0].rssi_dbm,
                     s.disp_pkts[0].snr_db);
            oled_puts(0, 2, line);
            oled_puts(0, 3, s.disp_pkts[0].summary);
        } else {
            oled_puts(0, 2, "no pkts yet");
        }

        /* Rows 4-5: second most recent decoded packet */
        if (s.disp_pkts[1].valid) {
            snprintf(line, sizeof(line), "!%08" PRIx32 " %4d %+3ddB",
                     s.disp_pkts[1].src_addr,
                     s.disp_pkts[1].rssi_dbm,
                     s.disp_pkts[1].snr_db);
            oled_puts(0, 4, line);
            oled_puts(0, 5, s.disp_pkts[1].summary);
        }

        /* Row 6: last RX RF quality (updated on every packet, decoded or not) */
        snprintf(line, sizeof(line), "RF:%4ddBm %+3ddB",
                 s.last_rx_rssi_dbm, s.last_rx_snr_db);
        oled_puts(0, 6, line);

        /* Row 7: (reserved — blank for now) */

        oled_flush();
    }
}

/* ── app_main ────────────────────────────────────────────────────────────── */

void app_main(void) {
    ESP_LOGI(TAG, "TLoRA v1 — Meshtastic Signal & Noise Analyzer");

    /* Read MAC address and derive node address */
    esp_read_mac(g_node_mac, ESP_MAC_WIFI_STA);
    g_node_addr = ((uint32_t)g_node_mac[2] << 24) |
                  ((uint32_t)g_node_mac[3] << 16) |
                  ((uint32_t)g_node_mac[4] << 8)  |
                   (uint32_t)g_node_mac[5];
    ESP_LOGI(TAG, "Node address: 0x%08" PRIX32 " (!%08" PRIx32 ")",
             g_node_addr, g_node_addr);

    /* Crypto init (derives AES key + channel hash from config.h values) */
    crypto_init();

    /* SX1276 SPI driver init */
    ESP_ERROR_CHECK(sx1276_init());

    /* OLED display init (non-fatal if not present) */
    if (oled_init() != ESP_OK)
        ESP_LOGW(TAG, "OLED init failed — display disabled");

    /* FreeRTOS primitives */
    g_event_queue    = xQueueCreate(16, sizeof(analyzer_event_t));
    g_rx_ready_queue = xQueueCreate(4,  sizeof(uint8_t));
    g_tx_done_sem    = xSemaphoreCreateBinary();
    g_stats_mutex    = xSemaphoreCreateMutex();
    configASSERT(g_event_queue && g_rx_ready_queue && g_tx_done_sem && g_stats_mutex);

    /* GPIO interrupt service */
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(LORA_PIN_DIO0, dio0_isr_handler, NULL));

    /* IRQ handler task — must be created first so its handle is available before
     * the ISR can fire */
    xTaskCreate(sx1276_irq_handler_task, "lora_irq", 2048, NULL, 5, &g_irq_task_handle);
    configASSERT(g_irq_task_handle);

    /* Other tasks */
    xTaskCreate(lora_rx_task,       "lora_rx",    4096, NULL, 4, NULL);
    xTaskCreate(lora_tx_task,       "lora_tx",    4096, NULL, 3, NULL);
    xTaskCreate(noise_sampler_task, "noise",       2048, NULL, 2, NULL);
    xTaskCreate(logger_task,        "logger",      4096, NULL, 1, NULL);
    xTaskCreate(display_task,       "display",     2048, NULL, 1, NULL);

    /* Start receiving */
    sx1276_set_rx_mode();
    ESP_LOGI(TAG, "All tasks started — entering RX continuous mode");
}
