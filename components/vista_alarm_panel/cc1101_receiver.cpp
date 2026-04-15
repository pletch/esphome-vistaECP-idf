// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf.
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "esphome/core/defines.h"
#ifdef CC1101_RECEIVER

#include "cc1101_receiver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

const char * const CC1101Receiver::TAG = "cc1101-rcv";

// RMT capture buffer size: 512 symbols on chips with DMA-capable RMT (ESP32-S3),
// 128 on all others (fits in 2 × 64-word hardware RMT SRAM blocks; sufficient for
// a full Honeywell burst of ≤110 symbols).
#ifdef SOC_RMT_SUPPORT_DMA
static constexpr size_t kRmtSymbols = 512;
#else
static constexpr size_t kRmtSymbols = 128;
#endif

CC1101Receiver::CC1101Receiver(VistaBus &bus,
                               int mosi, int miso, int sck, int csn, int gdo0)
    : bus_(bus)
    , radio_(mosi, miso, sck, csn, gdo0)
{}

void CC1101Receiver::log_config() const
{
    ESP_LOGCONFIG(TAG, "    MOSI: %d  MISO: %d  SCK: %d  CSN: %d  GDO0: %d",
                  radio_.mosi_pin(), radio_.miso_pin(),
                  radio_.sck_pin(),  radio_.csn_pin(),
                  radio_.gdo0_pin());
}

bool CC1101Receiver::begin()
{
    if (!radio_.begin()) 
    {
        ESP_LOGE(TAG, "CC1101 hardware init failed — receiver task not started");
        return false;
    }

    // Initialize RMT RX Channel to capture demodulated pulses from GDO0.
    // Buffer size and DMA flag are selected at compile time via kRmtSymbols above.
    rmt_rx_channel_config_t rx_chan_config = 
    {
        .gpio_num = static_cast<gpio_num_t>(radio_.gdo0_pin()),
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1 µs resolution
        .mem_block_symbols = kRmtSymbols,
        .intr_priority = 3,
#ifdef SOC_RMT_SUPPORT_DMA
        .flags = { .invert_in = false, .with_dma = true }
#else
        .flags = { .invert_in = false, .with_dma = false }
#endif
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_chan_config, &rmt_rx_chan_));

    rmt_rx_event_callbacks_t cbs = 
    {
        .on_recv_done = [](rmt_channel_handle_t, const rmt_rx_done_event_data_t *edata, 
                           void *user_data) {
            auto *self = static_cast<CC1101Receiver *>(user_data);
            if (self->task_handle_ != nullptr) {
                // Pass symbol count as notification value
                xTaskNotifyIndexedFromISR(self->task_handle_, 0, edata->num_symbols, eSetValueWithOverwrite, NULL);
            }
            return false;
        }
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rmt_rx_chan_, &cbs, this));
    ESP_ERROR_CHECK(rmt_enable(rmt_rx_chan_));

    xTaskCreate(rx_task,
                "cc1101_rx",
                5120,
                static_cast<void *>(this),
                5,
                &task_handle_);

    ESP_LOGI(TAG, "CC1101 Direct-Mode RMT receiver task started");
    return true;
}

void CC1101Receiver::rx_task(void *param)
{
    auto *self = static_cast<CC1101Receiver *>(param);

    // The receive buffer must live in internal SRAM — required for DMA-mode chips and
    // safe on all others (internal SRAM is always DMA-capable).  Stack allocation is
    // not used because rmt_receive() rejects non-DMA-capable addresses on DMA channels.
    const size_t symbols_size_bytes = kRmtSymbols * sizeof(rmt_symbol_word_t);
    rmt_symbol_word_t *symbols = static_cast<rmt_symbol_word_t *>(
        heap_caps_malloc(symbols_size_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (symbols == nullptr) 
    {
        ESP_LOGE(TAG, "Failed to allocate RMT DMA buffer. Task exiting.");
        vTaskDelete(NULL);
        return;
    }

    rmt_receive_config_t recv_config = 
    {
        // Filter out pulses shorter than 3 µs — suppresses sub-chip electrical
        // glitches on GDO0 without clipping real chips (~136 µs nominal).
        .signal_range_min_ns = 3000,
        // Terminate capture after 2 ms of idle (inter-repetition gaps are 30+ ms,
        // so each Honeywell packet repetition is captured as a separate frame).
        .signal_range_max_ns = 2000000,
    };

    while (1)
    {
        // Start RMT asynchronous receive. rmt_receive() takes buffer size in bytes.
        esp_err_t ret = rmt_receive(self->rmt_rx_chan_, symbols, symbols_size_bytes, &recv_config);
        if (ret != ESP_OK) {
            // Avoid crashing on transient errors (like ESP_ERR_INVALID_STATE during noise).
            ESP_LOGE(TAG, "rmt_receive failed: %s (0x%X)", esp_err_to_name(ret), ret);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Wait for RMT to finish (notification value is the number of symbols)
        uint32_t num_symbols = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (num_symbols == 0) continue;

        int8_t rssi = self->radio_.rssi_now();
        // Gate by RSSI to avoid processing noise floors from the asynchronous output
        {
            if (rssi < -87)
                continue;
        }
        if (num_symbols >= 40) 
        {
            ESP_LOGV(TAG, "RMT RX: %u symbols, RSSI: %d dBm", (unsigned int)num_symbols, rssi);
        }

        // Parse the pulse durations
        HoneywellPacket pkt = honeywell345_parse(symbols, num_symbols);

        if (pkt.valid)
        {
            int64_t now = esp_timer_get_time();
            bool is_duplicate = false;

            // Search history for a matching serial and status within the 2s window
            for (const auto& entry : self->dedupe_history_)
            {
                if (entry.serial == pkt.serial && 
                    entry.status == pkt.ecp_status && 
                    (now - entry.timestamp) < 2500000) 
                {
                    is_duplicate = true;
                    break;
                }
            }

            if (is_duplicate)
            {
                ESP_LOGV(TAG, "RF sensor: Ignoring duplicate repetition from 0x%05lX", (unsigned long)pkt.serial);
                continue;
            }
            else
                ESP_LOGI(TAG, "RF sensor: serial=0x%05lX  ecp_status=0x%02X  hb=%d  rssi=%d dBm",
                     (unsigned long)pkt.serial, pkt.ecp_status, pkt.heartbeat, rssi);

            // Add to circular history buffer
            self->dedupe_history_[self->dedupe_idx_] = {pkt.serial, pkt.ecp_status, now};
            self->dedupe_idx_ = (self->dedupe_idx_ + 1) % 16;

            self->bus_.sendRFmsg(pkt.serial, pkt.ecp_status);
        }
    }
}

#endif // CC1101_RECEIVER
