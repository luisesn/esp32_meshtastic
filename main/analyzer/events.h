#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* ── Event types ─────────────────────────────────────────────────────────── */

typedef enum {
    EVT_RX_PACKET,
    EVT_TX_DONE,
    EVT_NOISE_SAMPLE,
    EVT_ERROR,
} event_type_t;

typedef struct {
    int16_t  rssi_dbm;
    int8_t   snr_db;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t packet_id;
    uint8_t  flags;
    uint8_t  relay_node;               /* low 8 bits of last relay node addr, 0 = direct */
    bool     decoded;                   /* true = our channel, payload parsed */
    uint8_t  portnum;
    uint8_t  raw_payload[242];
    uint8_t  raw_len;
    char     payload_summary[96];       /* filled when decoded == true */
} rx_packet_t;

typedef struct {
    int16_t noise_floor_dbm;
    uint8_t sample_count;
} noise_data_t;

typedef struct {
    event_type_t type;
    uint32_t     ts_ms;
    union {
        rx_packet_t  rx;
        noise_data_t noise;
    };
} analyzer_event_t;

/* ── Per-packet display slot (ring of 2, updated by logger_task) ────────── */

typedef struct {
    uint32_t src_addr;
    int16_t  rssi_dbm;
    int8_t   snr_db;
    char     summary[22];   /* portnum-specific one-liner, max 21 chars + NUL */
    bool     valid;
} disp_pkt_t;

/* ── Shared statistics (display_task reads these periodically) ───────────── */

typedef struct {
    int16_t   last_noise_dbm;
    int16_t   last_rx_rssi_dbm;
    int8_t    last_rx_snr_db;
    uint32_t  rx_count;
    uint32_t  tx_count;
    disp_pkt_t disp_pkts[2];  /* [0]=most recent, [1]=second most recent */
} analyzer_stats_t;

/* ── Global handles (defined in main.c) ─────────────────────────────────── */

extern QueueHandle_t    g_event_queue;      /* producers → logger_task */
extern QueueHandle_t    g_rx_ready_queue;   /* irq_handler → lora_rx_task */
extern SemaphoreHandle_t g_tx_done_sem;     /* irq_handler → lora_tx_task */
extern TaskHandle_t     g_irq_task_handle;  /* for task notification from ISR */
extern analyzer_stats_t g_stats;
extern SemaphoreHandle_t g_stats_mutex;
extern uint32_t         g_node_addr;
extern uint8_t          g_node_mac[6];
