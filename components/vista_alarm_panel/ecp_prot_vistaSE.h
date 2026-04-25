// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

/*
VistaSE protocol implementation (legacy Vista SE panel family).

Derived from ProtocolBase<VistaSE> via CRTP, this class supplies the panel-specific
behaviour for the legacy ECP bus used by older Ademco Vista SE panels.

Key differences from Vista20P:
  - Data is sent at 2400 baud, 5 data bits, 1 stop bit (no parity on the SE bus).
  - Multi-character payloads are split into individual single-byte packets and
    re-queued so that each character is sent in its own bus write window.
  - The monitor task is synchronised via FreeRTOS task notifications (0x11 = break
    detected, 0x12 = low period started) rather than relying on break detection alone.
  - 0xFA expander frames are 3 bytes (sequence, type, address) — shorter than the
    5-byte Vista20P format.

Written by Tim Tim Pletcherer.
*/
#pragma once

#include "protocol_base.h"

// Implements the legacy Vista SE variant of the ECP bus protocol.
class VistaSE : public ProtocolBase<VistaSE>
{
public:
    using ProtocolBase<VistaSE>::ProtocolBase;

    bool legacy_protocol = true;

    // Drain the send queue into pkt.  Because the SE protocol sends one character
    // per bus write window, multi-character payloads are split into individual
    // single-byte packets and re-queued so they are sent one per cycle.
    void check_send_Q_impl(SendPacket &pkt)
    {
        while (uxQueueMessagesWaiting(this->vistabus_.sendQueue))
        {
            if (!this->req_to_send)
            {
                xQueueReceive(this->vistabus_.sendQueue, &pkt, 0);
                this->req_to_send = true;
            }
            if (pkt.size > 1) // split multi-byte payload into individual chars
            {
                SendPacket next_char;
                for (int i = 0; i < pkt.size; i++)
                {
                    next_char.keypadaddress = pkt.keypadaddress;
                    next_char.type          = pkt.type;
                    next_char.sequence      = pkt.sequence;
                    next_char.size          = 1;
                    next_char.payload[0]    = pkt.payload[i];
                    xQueueSend(this->vistabus_.sendQueue, &next_char, pdMS_TO_TICKS(0));
                }
                xQueueReceive(this->vistabus_.sendQueue, &pkt, 0);
                break;
            }
            else
                break;
        }
    }

    // Wait for a UART event and return the number of bytes read into buf[0].
    //
    // UART_DATA  — read the byte; discard null bytes (SE bus idle filler).
    // UART_BREAK — notify the monitor task that a break has been detected, then
    //              measure the high-time that follows via GPIO edge interrupts:
    //     > 20 ms high  → valid write / 2400-baud window:
    //         if req_to_send & type==1: fire a one-shot timer (3 ms delay) and
    //             write a single 2400/5-bit character; restore UART config after.
    //         else: wait for a rising edge and measure ~6 ms for 2400 baud switch.
    //     timeout (no rising edge within 10 ms) → expander mark-pulse window.
    int handle_UART_events_impl(const SendPacket &pkt_to_send, uint8_t *buf)
    {
        int bytes = 0;
        uart_event_t event;
        xQueueReceive(this->vistabus_.uartevtQueue, (void *)&event,
                      pdMS_TO_TICKS(kPulseCyclePeriod));
        switch (event.type)
        {
            case UART_DATA:
                bytes = uart_read_bytes(this->vistabus_.uart_num, buf, 1, 0);
                if (buf[0] == 0)
                    bytes = 0; // discard idle null bytes
                break;

            case UART_BREAK:
            {
                static GpioTaskArgs taskargs;
                taskargs.task_handle = this->vistabus_.rx_tx_task_Handle;
                taskargs.pin         = this->vistabus_.rx_pin;

                // Notify the monitor task that a break has been detected so it can
                // prepare to switch baud rate and read SE keypad data.
                if (this->vistabus_.monitor_rx_task_Handle != nullptr)
                {
                    uint32_t val = 0x11 << 8;
                    xTaskNotify(this->vistabus_.monitor_rx_task_Handle, val,
                                eSetValueWithOverwrite);
                }

                if (gpio_get_level(this->vistabus_.rx_pin))
                {
                    int64_t high_start = esp_timer_get_time();
                    // Wait for the falling edge that ends the mark/high period.
                    gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_NEGEDGE);
                    gpio_isr_handler_add(this->vistabus_.rx_pin, gpio_isr_handler,
                                         (void *)&taskargs);
                    // Discard any stale task notification left by a previous mark_pulse or 
                    // BREAK handling cycle.
                    xTaskNotifyStateClear(nullptr);
                    if (xTaskNotifyWait(0, 0xFFFFFFFF, nullptr,
                                        pdMS_TO_TICKS(535)) == pdPASS)
                    {
                        // Notify monitor task that the low period has started.
                        if (this->vistabus_.monitor_rx_task_Handle != nullptr)
                        {
                            uint32_t val = 0x12 << 8;
                            xTaskNotify(this->vistabus_.monitor_rx_task_Handle, val,
                                        eSetValueWithOverwrite);
                        }

                        int64_t start     = esp_timer_get_time();
                        int64_t high_time = start - high_start;

                        if (high_time > 20000)
                        {
                            gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_POSEDGE);
                            if (this->req_to_send && pkt_to_send.type == 1
                                    && (high_time < kSEWriteWindowBelowUs || high_time > kSEWriteWindowAboveUs))
                            {
                                // High-time in the valid write window — send one character.
                                const esp_timer_create_args_t oneshot_timer_args =
                                {
                                    .callback = &timer_isr_handler,
                                    .arg      = (void *)this->vistabus_.rx_tx_task_Handle,
                                    .dispatch_method = ESP_TIMER_ISR,
                                    .name     = "one-shot"
                                };
                                esp_timer_handle_t oneshot_timer;
                                esp_timer_create(&oneshot_timer_args, &oneshot_timer);
                                esp_timer_start_once(oneshot_timer, 3500); // 3.5 ms pre-send delay

                                xTaskNotifyWait(0, 0xFFFFFFFF, nullptr, pdMS_TO_TICKS(10));
                                gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_ANYEDGE);
                                bool data_written = true;
                                uart_set_baudrate(this->vistabus_.uart_num, kEcpBaudLegacy);
                                uart_set_stop_bits(this->vistabus_.uart_num, UART_STOP_BITS_1);
                                uart_set_word_length(this->vistabus_.uart_num, UART_DATA_5_BITS);
                                keypad_write_SE(this->vistabus_.uart_num, pkt_to_send.payload);

                                // Retry up to twice if the bus is not acknowledged.
                                if (xTaskNotifyWait(0, 0xFFFFFFFF, nullptr, 0) != pdTRUE)
                                {
                                    keypad_write_SE(this->vistabus_.uart_num, pkt_to_send.payload);
                                    if (xTaskNotifyWait(0, 0xFFFFFFFF, nullptr, 0) != pdTRUE)
                                        keypad_write_SE(this->vistabus_.uart_num, pkt_to_send.payload);
                                    else
                                        data_written = false;
                                }
                                else
                                    data_written = false;

                                if (data_written)
                                    this->req_to_send = false;

                                uart_set_word_length(this->vistabus_.uart_num, UART_DATA_8_BITS);
                                uart_set_stop_bits(this->vistabus_.uart_num, UART_STOP_BITS_2);
                                uart_set_baudrate(this->vistabus_.uart_num, kEcpBaudStandard);

                                esp_timer_stop(oneshot_timer);
                                esp_timer_delete(oneshot_timer);
                            }
                            else if (xTaskNotifyWait(0, 0xFFFFFFFF, nullptr,
                                                     pdMS_TO_TICKS(10)) == pdPASS)
                            {
                                int64_t end = esp_timer_get_time();
                                if (end - start > kBaudSwitchMarkMinUs && end - start < kBaudSwitchMarkMaxUs)
                                {
                                    // ~6 ms mark: panel transmitting a 2400 baud packet.
                                    if (!this->legacy_programmode)
                                    {
                                        uart_flush(this->vistabus_.uart_num);
                                        uart_read_bytes_event(this->vistabus_.uart_num, buf, 1,
                                                              pdMS_TO_TICKS(4),
                                                              this->vistabus_.uartevtQueue); // flush leading zero
                                        uart_set_baudrate(this->vistabus_.uart_num, kEcpBaudLegacy);
                                        bytes = uart_read_bytes_event(this->vistabus_.uart_num, buf, 1,
                                                                      pdMS_TO_TICKS(15),
                                                                      this->vistabus_.uartevtQueue);
                                        uart_set_baudrate(this->vistabus_.uart_num, kEcpBaudStandard);
                                        this->is_2400 = true;
                                    }
                                    else
                                    {
                                        uart_read_bytes_event(this->vistabus_.uart_num, buf, 1,
                                                              pdMS_TO_TICKS(4),
                                                              this->vistabus_.uartevtQueue); // flush leading zero
                                        bytes = uart_read_bytes_event(this->vistabus_.uart_num, buf, 1,
                                                                      pdMS_TO_TICKS(15),
                                                                      this->vistabus_.uartevtQueue);
                                    }
                                }
                            }
                            else if (this->req_to_send && !this->pulse_marked
                                     && pkt_to_send.type == 2)
                            {
                                // Expansion device pulse-mark window.
                                mark_pulse(this->vistabus_.uart_num, pkt_to_send.keypadaddress);
                                this->pulse_marked    = true;
                                this->pulse_mark_time = esp_timer_get_time();
                            }
                        }
                    }
                    gpio_isr_handler_remove(this->vistabus_.rx_pin);
                }
                break;
            }
            default:
                break;
        }
        return bytes;
    }

    // Block until a task notification arrives from rx_tx_task.
    //
    // Notification 0x11 → break detected: reconfigure the external UART for
    //   2400 baud / 5-bit / 1-stop, wait for the 0x12 low-period notification,
    //   then read up to 3 bytes of SE keypad data and enqueue them directly.
    //   The UART is restored to 4800 baud afterwards.
    // Other notifications → read one byte from the external UART at 4800 baud
    //   and return it for the caller to dispatch.
    int monitor_task_sync_impl(uint8_t *buf, uint32_t &val)
    {
        ReceivedPacket rcvd_extPkt;
        rcvd_extPkt.type = 1;
        int bytes        = 0;

        xTaskNotifyWait(0xFFFFFFFF, 0xFFFFFFFF, &val, portMAX_DELAY);
        if (static_cast<uint8_t>(val >> 8) == 0x11) // break detected on main bus
        {
            uart_set_baudrate(this->vistabus_.ext_uart_num, kEcpBaudLegacy);
            uart_set_stop_bits(this->vistabus_.ext_uart_num, UART_STOP_BITS_1);
            uart_set_word_length(this->vistabus_.ext_uart_num, UART_DATA_5_BITS);
            buf[0] = 0;
            memset(rcvd_extPkt.payload, '\0', sizeof(rcvd_extPkt.payload));

            // Wait for the low-period-started notification before reading.
            xTaskNotifyWait(0xFFFFFFFF, 0, &val, portMAX_DELAY);
            if (static_cast<uint8_t>(val >> 8) == 0x12)
            {
                rcvd_extPkt.source = 0xDD; // VistaSE protocol keypad data
                bytes = this->get_packet(&rcvd_extPkt, buf, 0, 3,
                                         this->vistabus_.ext_uart_num,
                                         pdMS_TO_TICKS(8)); // minimum 4 ms for data to arrive
            }
            uart_set_word_length(this->vistabus_.ext_uart_num, UART_DATA_8_BITS);
            uart_set_stop_bits(this->vistabus_.ext_uart_num, UART_STOP_BITS_2);
            uart_set_baudrate(this->vistabus_.ext_uart_num, kEcpBaudStandard);

            if (bytes)
            {
                uart_flush(this->vistabus_.ext_uart_num);
                xQueueSend(this->vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(0));
                bytes = 0;
            }
        }
        else
        {
            bytes = uart_read_bytes(this->vistabus_.ext_uart_num, buf, 1, pdMS_TO_TICKS(20));
        }
        return bytes;
    }

    // Respond to a 0xFA expander poll in-line for timing (SE variant).
    //
    // The SE format uses a fixed expander address of 0x01 and a 3-byte payload.
    // Handled types:
    //   0xF1 — zone-state request: dequeue one DeviceMsg and reply with zone state.
    //   0xF7 — tamper/fault poll: reply with stored NO/NC fault bitmasks.
    void quick_decodeFA_impl(const char *cbuf)
    {
        VistaBus::EmulatedExpander *expander = vistabus_.getExpander(0x01);
        char    lcbuf[5];
        char    type         = cbuf[3];
        char    exp_sequence = (cbuf[2] == 0x20 ? 0x35 : 0x30);

        if (type == 0xF1)
        {
            // Zone-state request: consume one pending DeviceMsg and reply.
            // SE expander zones run 10–17, so shift the zone-position calculation
            // by one relative to V20P (where zones run 9–16) so zone 10 lands at
            // z=1 and zone 17 wraps to z=0 (encoded via lcbuf[2] = 0x01).
            DeviceMsg expMsg;
            xQueueReceive(vistabus_.deviceMsgQueue, &expMsg, pdMS_TO_TICKS(25));
            lcbuf[0] = 0x01;
            lcbuf[1] = exp_sequence;
            uint8_t z = (expMsg.source - 1) & 0x07;
            lcbuf[2] = z ? 0 : 0x01;
            lcbuf[3] = (z << 5) ^ (0x10 * expMsg.msg);
            lcbuf[4] = calc_chksum_two(lcbuf, 0, 4);
            uart_write_bytes(vistabus_.uart_num, lcbuf, 5);
        }
        else if (type == 0xF7)
        {
            // Fault / supervision poll: reply with stored bitmasks.
            lcbuf[0] = 0xF0;
            lcbuf[1] = exp_sequence;
            lcbuf[2] = expander->zone_status_bits;
            lcbuf[3] = expander->supervision_bits;
            lcbuf[4] = calc_chksum_two(lcbuf, 0, 4);
            uart_write_bytes(vistabus_.uart_num, lcbuf, 5);
        }
    }

    // Read the remainder of a legacy 0xFA expander poll frame, validate the
    // two-byte checksum, notify the monitor task, and trigger an in-line expander
    // response if EXP emulation is active.  Always enqueues a ReceivedPacket.
    void dispatchFA_impl()
    {
        uint8_t data[kFALegacyMessageLength + 1];
        ReceivedPacket received_packet;
        received_packet.type       = 0;
        received_packet.payload[0] = 0xFA;
        uint8_t length             = kFALegacyMessageLength;

        int  rxBytes = this->get_packet_event(&received_packet, data, 1, length - 1,
                                              vistabus_.uart_num,
                                              pdMS_TO_TICKS(kUartDelay),
                                              vistabus_.uartevtQueue);
        bool chk = valid_chksum(received_packet.payload, 0, rxBytes + 1);
        if (chk)
        {
            uint32_t val = 0xFA << 16
                         | (received_packet.payload[1] << 8)
                         | received_packet.payload[3];
            if (vistabus_.monitor_rx_task_Handle != nullptr)
                xTaskNotify(vistabus_.monitor_rx_task_Handle, val,
                            eSetValueWithOverwrite);
            if (vistabus_.EXPemulation)
                this->quick_decodeFA(received_packet.payload);
            received_packet.source = 0xFA;
        }
        else
            received_packet.source = 0xCF;

        xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
    }

private:
    // Translate a single ASCII keypad character to the SE 5-bit bus encoding and
    // write it to the UART.  Returns the result of uart_wait_tx_done.
    //
    // Character mapping (ASCII → SE code):
    //   '0'–'9'  (0x30–0x39) → 0x00–0x09
    //   '*'      (0x2A)       → 0x0A
    //   '#'      (0x23)       → 0x0B
    //   'F'      (0x46)       → 0x0C  (Function)
    //   'M'      (0x4D)       → 0x0D  (Memory)
    //   'P'      (0x50)       → 0x0E  (Partition)
    //   'G'      (0x47)       → 0x0F  (Go)
    //   'A'–'D'  (0x41–0x44) → 0x1C–0x1F
    int keypad_write_SE(uart_port_t uart_n, const char *character)
    {
        char outbuffer[2];
        memset(outbuffer, '\0', sizeof(outbuffer));
        if (character[0] >= 0x30 && character[0] <= 0x39)
            outbuffer[0] = (character[0] - 0x30);
        else if (character[0] == 0x23)
            outbuffer[0] = 0x0B;
        else if (character[0] == 0x2A)
            outbuffer[0] = 0x0A;
        else if (character[0] == 0x46)
            outbuffer[0] = 0x0C;
        else if (character[0] == 0x4D)
            outbuffer[0] = 0x0D;
        else if (character[0] == 0x50)
            outbuffer[0] = 0x0E;
        else if (character[0] == 0x47)
            outbuffer[0] = 0x0F;
        else if (character[0] >= 0x41 && character[0] <= 0x44)
            outbuffer[0] = (character[0] - 0x25);
        uart_tx_chars(uart_n, outbuffer, 1);
        return uart_wait_tx_done(uart_n, pdMS_TO_TICKS(10));
    }
};
