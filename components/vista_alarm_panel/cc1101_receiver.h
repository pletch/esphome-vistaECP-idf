// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf.
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once
#include "esphome/core/defines.h"
#ifdef CC1101_RECEIVER

#include "cc1101.h"
#include "driver/rmt_rx.h"
#include "honeywell_345.h"
#include "vista_bus.h"

// ---------------------------------------------------------------------------
// CC1101Receiver — FreeRTOS task wrapper for CC1101 hardware RF reception.
//
// The CC1101 is configured in Asynchronous Serial Mode (PKTCTRL0=0x32) with
// GDO0 set to raw demodulated OOK output (IOCFG0=0x0D).  GDO0 directly
// mirrors the OOK carrier: high when carrier is present, low otherwise.
// The ESP32 RMT peripheral captures pulse widths from GDO0 and notifies the
// receive task via on_recv_done callback.  The task decodes the pulse-width
// stream via honeywell345_parse() and forwards all valid packets — including
// supervision heartbeats — to VistaBus via sendRFmsg().  The panel requires
// heartbeat packets to maintain sensor supervision; suppressing them would
// cause trouble conditions for enrolled sensors.
//
// From sendRFmsg() the existing emulated RF receiver flow handles delivery to
// the physical panel (F1 poll → ECP bus write → monitor capture →
// dispatch_extFB → receiveQueue → ZoneManager::on_rf_zone_packet()).
//
// begin() must be called after vistabus_.emulateRFR() has been invoked so
// that the emulated RF receiver address is already configured before any
// packets can arrive.
// ---------------------------------------------------------------------------

// kRmtSymbols is defined in cc1101_receiver.cpp based on SOC_RMT_SUPPORT_DMA:
//   512 symbols on chips with RMT DMA (ESP32-S3) — buffer in system RAM.
//   128 symbols on all other chips       — buffer in on-chip RMT SRAM
//                                          (2 × 64-word hardware blocks on original ESP32;
//                                           sufficient for a full Honeywell burst of ≤110 symbols).

class CC1101Receiver 
{
public:
    // bus:      VistaBus reference — used only to call sendRFmsg().
    // mosi/miso/sck/csn/gdo0: SPI pin numbers for the CC1101 module.
    // spi_host: IDF SPI host to use (SPI2_HOST or SPI3_HOST, default SPI2_HOST).
    CC1101Receiver(VistaBus &bus, int mosi, int miso, int sck, int csn, int gdo0,
                   spi_host_device_t spi_host = SPI2_HOST);

    // Initialise the CC1101 hardware and start the receive task.
    // Returns false if the CC1101 does not respond (check wiring / power).
    bool begin();

    // Suspend/resume the receive task around flash-cache-disabling events
    // (e.g. OTA writes).  Safe to call from any task context.
    void suspend() { if (task_handle_) vTaskSuspend(task_handle_); }
    void resume()  { if (task_handle_) vTaskResume(task_handle_);  }

    // Log SPI and GDO0 pin assignments via ESP_LOGCONFIG.
    void log_config() const;


private:
    static void rx_task(void *param);

    VistaBus    &bus_;
    CC1101       radio_;
    TaskHandle_t task_handle_ {nullptr};
    rmt_channel_handle_t rmt_rx_chan_ {nullptr};

    // De-duplication state
    struct DedupeEntry {
        uint32_t serial;
        uint8_t  status;
        int64_t  timestamp;
    };
    DedupeEntry dedupe_history_[16] {};
    uint8_t     dedupe_idx_ {0};

    static const char * const TAG;
};

#endif // CC1101_RECEIVER
