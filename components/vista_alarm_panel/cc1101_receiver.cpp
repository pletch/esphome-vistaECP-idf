// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf.
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "esphome/core/defines.h"
#ifdef CC1101_RECEIVER

#include "cc1101_receiver.h"
#include "constants.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

CC1101Receiver::CC1101Receiver(VistaBus &bus,
                               int mosi, int miso, int sck, int csn, int gdo0,
                               spi_host_device_t spi_host)
    : bus_(bus)
    , radio_(mosi, miso, sck, csn, gdo0, spi_host)
{}

void CC1101Receiver::log_config() const
{
    ESP_LOGCONFIG(TAG, "    MOSI: %d  MISO: %d  SCK: %d  CSN: %d  GDO0: %d",
                  radio_.mosi_pin(), radio_.miso_pin(),
                  radio_.sck_pin(),  radio_.csn_pin(),
                  radio_.gdo0_pin());
    ESP_LOGCONFIG(TAG, "    RSSI threshold: %d dBm", rssi_threshold_);
}

bool CC1101Receiver::begin()
{
    if (!radio_.begin()) 
    {
        ESP_LOGE(TAG, "CC1101 hardware init failed — receiver task not started");
        return false;
    }

    // Initialize RMT RX Channel to capture demodulated pulses from GDO0.
    // Buffer size and DMA flag are selected at compile time via kCc1101RmtSymbols.
    rmt_rx_channel_config_t rx_chan_config =
    {
        .gpio_num = static_cast<gpio_num_t>(radio_.gdo0_pin()),
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000, // 1 µs resolution
        .mem_block_symbols = kCc1101RmtSymbols,
        .intr_priority = 3,
#ifdef SOC_RMT_SUPPORT_DMA
        .flags = { .invert_in = false, .with_dma = true }
#else
        .flags = { .invert_in = false, .with_dma = false }
#endif
    };
    // RMT init must degrade gracefully: the CC1101 is an optional peripheral, so a
    // failure here must not abort the firmware and take down wired-zone monitoring
    // and ECP panel comms.  Return false (like the radio_.begin() path above) and
    // let setup() simply skip the RF direct task; the panel keeps running.
    esp_err_t err = rmt_new_rx_channel(&rx_chan_config, &rmt_rx_chan_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "rmt_new_rx_channel failed: %s — RF receiver disabled",
                 esp_err_to_name(err));
        return false;
    }

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
    err = rmt_rx_register_event_callbacks(rmt_rx_chan_, &cbs, this);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "rmt_rx_register_event_callbacks failed: %s — RF receiver disabled",
                 esp_err_to_name(err));
        rmt_del_channel(rmt_rx_chan_);
        rmt_rx_chan_ = nullptr;
        return false;
    }
    err = rmt_enable(rmt_rx_chan_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "rmt_enable failed: %s — RF receiver disabled", esp_err_to_name(err));
        rmt_del_channel(rmt_rx_chan_);
        rmt_rx_chan_ = nullptr;
        return false;
    }

    xTaskCreate(rx_task,
                "cc1101_rx",
                kCc1101RxTaskStackSize,
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
    const size_t symbols_size_bytes = kCc1101RmtSymbols * sizeof(rmt_symbol_word_t);
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
        .signal_range_min_ns = kCc1101RmtSignalMinNs,
        .signal_range_max_ns = kCc1101RmtSignalMaxNs,
    };

    // Initialise the watchdog clock so the first 90-min window starts now,
    // not from boot (which would be unfair if the rx_task starts late).
    self->last_packet_us_ = esp_timer_get_time();

    bool receive_armed = false;
    while (1)
    {
        if (!receive_armed)
        {
            // Start RMT asynchronous receive. rmt_receive() takes buffer size in bytes.
            esp_err_t ret = rmt_receive(self->rmt_rx_chan_, symbols, symbols_size_bytes, &recv_config);
            if (ret != ESP_OK) {
                // Avoid crashing on transient errors (like ESP_ERR_INVALID_STATE during noise).
                ESP_LOGE(TAG, "rmt_receive failed: %s (0x%X)", esp_err_to_name(ret), ret);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            receive_armed = true;
        }

        // Wait for RMT to finish — finite timeout so the task wakes periodically
        // for the watchdog even if the chip's GDO0 has stopped firing entirely.
        uint32_t num_symbols = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kCc1101HealthCheckPeriodMs));

        if (num_symbols == 0)
        {
            // Notification timeout — no RMT activity.  Run health check and loop;
            // the original rmt_receive is still pending so we must not re-arm.
            self->check_health();
            continue;
        }

        // Receive completed; next iteration must re-arm rmt_receive.
        receive_armed = false;

        int8_t rssi = self->radio_.rssi_now();
        // Gate by RSSI to avoid processing noise floors from the asynchronous output
        {
            if (rssi < self->rssi_threshold_)
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
            // Tier-2 watchdog: any valid decode (including duplicates and
            // heartbeats) is proof the chip and antenna are working.
            self->last_packet_us_ = now;
            bool is_duplicate = false;

            // Two-tier dedup:
            //   Burst tier  (serial only, 300 ms): collapses all retransmissions
            //     within a single RF burst, including interleaved fault/clear status
            //     bytes that some sensors emit during state transitions.
            //   Long-range  (serial+status, 2.5 s): suppresses exact repeats that
            //     slip through after the burst window.
            for (const auto& entry : self->dedupe_history_)
            {
                if (entry.serial != pkt.serial) continue;
                int64_t age = now - entry.timestamp;
                if (age < kDedupeBurstWindowUs)                                    { is_duplicate = true; break; }
                if (entry.status == pkt.ecp_status && age < kDedupeLongRangeWindowUs) { is_duplicate = true; break; }
            }

            if (is_duplicate)
            {
                ESP_LOGV(TAG, "RF sensor: Ignoring duplicate repetition from 0x%05lX", (unsigned long)pkt.serial);
                continue;
            }
            else
                ESP_LOGI(TAG, "RF sensor: serial=0x%05lX (%lu)  ecp_status=0x%02X  hb=%d  rssi=%d dBm",
                     (unsigned long)pkt.serial, (unsigned long)pkt.serial, pkt.ecp_status, pkt.heartbeat, rssi);

            // Add to circular history buffer
            self->dedupe_history_[self->dedupe_idx_] = {pkt.serial, pkt.ecp_status, now};
            self->dedupe_idx_ = (self->dedupe_idx_ + 1) % kDedupeHistorySize;

            // Post all valid packets (including heartbeats) to rf_direct_queue.
            // Non-heartbeat packets: rf_direct_task publishes zone state to HA
            //   immediately, bypassing the panel F1 poll → FA/FB round-trip.
            // Heartbeat packets: on_rf_direct() stores RSSI in the cache then
            //   returns early without updating zone state, so that on_rf_zone_packet()
            //   can append RSSI to the rf_messages heartbeat string.
            if (self->bus_.rf_direct_queue != nullptr)
            {
                RfDirectMsg direct{pkt.serial, pkt.ecp_status, rssi};
                xQueueSend(self->bus_.rf_direct_queue, &direct, 0);  // non-blocking
            }

            // ECP path: always forward to the panel via the emulated RF receiver.
            self->bus_.sendRFmsg(pkt.serial, pkt.ecp_status);
        }
    }
}

// ---------------------------------------------------------------------------
// Health watchdog — called from rx_task when the notify timeout expires.
//
// Tier 1 (chip-state):  Read MARCSTATE.  If the chip has dropped out of RX,
//   strobe SIDLE+SRX (kick_rx) to cycle the state machine.  Cheap; runs every
//   minute and catches the common "chip lost RX state" wedge within 60 s.
//
// Tier 2 (packet-absence):  If no valid packet has been decoded for the
//   90-minute watchdog window, do a full radio_.begin() re-init.  This covers
//   deeper failures (register corruption, antenna issue, etc.) that the
//   MARCSTATE reading alone cannot diagnose.  If begin() itself fails, SPI is
//   broken and a reboot is the only recovery.
// ---------------------------------------------------------------------------
void CC1101Receiver::check_health()
{
    const int64_t now = esp_timer_get_time();
    const int64_t silence_us = now - last_packet_us_;

    // Tier 2 first — if we've been silent past the 90-min window, the chip
    // has either wedged silently or something deeper is wrong.  A full
    // re-init is the right level of intervention.  Skipped entirely when no
    // physical RF sensors are configured (silence is expected then).
    if (packet_watchdog_enabled_ && silence_us > kCc1101PacketWatchdogUs)
    {
        ESP_LOGE(TAG, "Watchdog: no valid packet in %lld s — re-initialising CC1101",
                 silence_us / 1000000);
        if (!radio_.begin())
        {
            ESP_LOGE(TAG, "CC1101 begin() failed during watchdog recovery — rebooting");
            esp_restart();  // SPI is dead; reboot is the only option.
        }
        // Reset the watchdog so the next 90-min window starts from now,
        // not from a long-stale last_packet_us_.
        last_packet_us_ = now;
        return;
    }

    // Tier 1 — cheap MARCSTATE check.  If the chip has slipped out of RX,
    // a SIDLE/SRX cycle almost always recovers it.
    const uint8_t state = radio_.marcstate();
    if (state != CC1101Status::MARCSTATE_RX)
    {
        ESP_LOGW(TAG, "Watchdog: MARCSTATE=0x%02X (expected 0x%02X) — kicking back to RX",
                 state, CC1101Status::MARCSTATE_RX);
        radio_.kick_rx();
    }
}

#endif // CC1101_RECEIVER
