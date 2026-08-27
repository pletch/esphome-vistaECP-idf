// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

/*
Vista20P protocol implementation (Vista 15P / 20P panel family).

Derived from ProtocolBase<Vista20P> via CRTP, this class supplies the panel-specific
behaviour for the modern ECP bus running at 4800 baud, even parity, 2 stop bits.

Key characteristics:
  - The panel signals an available keypad write window with a ~6 ms mark pulse on
    the RX line.  The pulse duration is measured via GPIO interrupts to distinguish
    the write window from the 2400 baud preamble used for F2/F8/F9 packets.
  - Multiple keystroke packets for the same destination address are consolidated into
    a single up-to-24-byte frame before transmission (reduces bus round-trips).
  - 0xFA expander poll frames are 5 bytes: address, sequence, two flag bytes,
    checksum.  Responses must be generated in-line (quick_decodeFA_impl) because
    the panel's timing window is too tight to round-trip through the receive queue.

Written by Tim Pletcher.
*/
#pragma once

#include "protocol_base.h"

// Implements the Vista 15P / 20P variant of the ECP bus protocol.
class Vista20P : public ProtocolBase<Vista20P> {
 public:
  using ProtocolBase<Vista20P>::ProtocolBase;

  bool legacy_protocol = false;

  // Drain the send queue into pkt, consolidating entries for the same keypad
  // address into a single payload (up to 24 bytes) to reduce bus traffic.
  // Sequence numbers are iterated when consolidation collapses duplicate sends.
  void check_send_Q_impl(SendPacket &pkt) {
    while (uxQueueMessagesWaiting(this->vistabus_.sendQueue)) {
      if (!req_to_send) {
        xQueueReceive(this->vistabus_.sendQueue, &pkt, 0);
        req_to_send = true;
        // Iterate the sequence byte if this packet matches the last
        // send — prevents the panel ignoring a rapid re-send.
        if (pkt.sequence == last_sequence && pkt.keypadaddress == last_address)
          pkt.sequence += 0x40;
        this->last_sequence = pkt.sequence;
        this->last_address = pkt.keypadaddress;
      } else {
        // Peek at the next queued packet; if it targets the same keypad
        // and the combined payload fits in 24 bytes, merge it in.
        //
        // Never merge a type-2 pulse, in either direction.  It carries no
        // payload, so a merge would dequeue it and append zero bytes --
        // silently consuming the F1 nudge without ever issuing it, leaving
        // the device message to wait at least another kPulseCyclePeriod
        // for the next one.  Only same-keypadaddress packets merge, so
        // this needs a colliding address to bite; requestF1() putting the
        // nudge at the front of the queue makes it the packet peeked here
        // every time, which is reason enough to be explicit.
        SendPacket next_pkt;
        xQueuePeek(this->vistabus_.sendQueue, &next_pkt, pdMS_TO_TICKS(0));
        if (pkt.type != 2 && next_pkt.type != 2 && next_pkt.keypadaddress == pkt.keypadaddress &&
            (next_pkt.size + pkt.size) <= 24) {
          xQueueReceive(this->vistabus_.sendQueue, &next_pkt, 0);
          memcpy(pkt.payload + pkt.size, next_pkt.payload, next_pkt.size);
          pkt.size += next_pkt.size;
        } else {
          break;
        }
      }
    }
  }

  // Wait for a UART event and return the number of bytes read into buf[0].
  //
  // UART_DATA  — normal 4800 baud byte; read and return it.
  // UART_BREAK — measure the mark-pulse width via GPIO edge interrupts:
  //     ~6 ms  → panel opened a 2400 baud data window; switch baud rate,
  //              flush the leading zero byte, read the packet header, restore.
  //     no rising edge within 10 ms → panel opened the keypad write window;
  //              mark the pulse and record the time for ACK tracking.
  int handle_UART_events_impl(const SendPacket &pkt_to_send, uint8_t *buf) {
    int bytes = 0;
    uart_event_t event{};
    // The receive can time out (the panel's poll cycle is longer than some
    // pulse periods).  Bail out rather than switching on an event that was
    // never written: 'event' would hold stack garbage, which can match
    // UART_BREAK and run the whole break-handling path on nothing.
    if (xQueueReceive(this->vistabus_.uartevtQueue, (void *) &event, pdMS_TO_TICKS(kPulseCyclePeriod)) != pdPASS)
      return 0;

    switch (event.type) {
      case UART_DATA:
        bytes = uart_read_bytes(this->vistabus_.uart_num, buf, 1, 0);
        break;

      case UART_BREAK: {
        static GpioTaskArgs taskargs;
        taskargs.task_handle = this->vistabus_.rx_tx_task_Handle;
        taskargs.pin = this->vistabus_.rx_pin;
        if (gpio_get_level(this->vistabus_.rx_pin)) {
          // Wait for the falling edge that ends the break/mark period.
          gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_NEGEDGE);
          gpio_isr_handler_add(this->vistabus_.rx_pin, gpio_isr_handler, (void *) &taskargs);
          // Discard any stale task notification left by a previous
          // mark_pulse or BREAK handling cycle.
          xTaskNotifyStateClear(nullptr);
          if (xTaskNotifyWait(0, 0xFFFFFFFF, nullptr, pdMS_TO_TICKS(535)) == pdPASS) {
            int64_t start = esp_timer_get_time();
            // Now wait for the rising edge that ends the low period.
            gpio_set_intr_type(this->vistabus_.rx_pin, GPIO_INTR_POSEDGE);
            if (xTaskNotifyWait(0, 0xFFFFFFFF, nullptr, pdMS_TO_TICKS(10)) == pdPASS) {
              int64_t end = esp_timer_get_time();
              if (end - start > kBaudSwitchMarkMinUs && end - start < kBaudSwitchMarkMaxUs) {
                // 6 ms mark: panel is transmitting a 2400 baud packet.
                uart_flush(this->vistabus_.uart_num);
                uart_read_bytes_event(this->vistabus_.uart_num, buf, 1, pdMS_TO_TICKS(4),
                                      this->vistabus_.uartevtQueue);  // flush leading zero
                this->vistabus_.set_baud_fast(false);

                // Byte 0 of the frame arrives at 2400; bytes 1-4 at
                // 4800.  The divider has to be back at 4800 before
                // the panel clocks out byte 1 — about 208 us of
                // margin — so the restore happens the moment the
                // UART_DATA event lands, ahead of actually draining
                // the byte.  The byte was fully sampled and copied
                // into the driver's ring buffer before the event was
                // posted, so reading it after the divider changes is
                // safe; holding 2400 across the read only widened the
                // window for nothing.
                uart_event_t pre_evt{};
                bytes = 0;

                while (xQueueReceive(this->vistabus_.uartevtQueue, (void *) &pre_evt, pdMS_TO_TICKS(10)) == pdPASS) {
                  // BREAK and error events are not the preamble
                  // byte; keep waiting for the one that is.
                  if (pre_evt.type != UART_DATA)
                    continue;

                  this->vistabus_.set_baud_fast(true);
                  bytes = uart_read_bytes(this->vistabus_.uart_num, buf, 1, 0);
                  break;
                }

                if (bytes <= 0)
                  this->vistabus_.set_baud_fast(true);

                this->is_2400 = true;
              }
            } else if (this->req_to_send && !this->pulse_marked) {
              // No rising edge within 10 ms — panel opened a write window.
              mark_pulse(this->vistabus_.uart_num, pkt_to_send.keypadaddress);
              this->pulse_marked = true;
              this->pulse_mark_time = esp_timer_get_time();
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

  // Wait for rx_tx_task to say what the panel just asked for, then read the
  // addressed device's reply off the green wire.
  //
  // Notification first, mirroring VistaSE::monitor_task_sync_impl.  The previous
  // order was inverted -- read a byte, then go looking for the context it
  // belonged to -- and a byte whose notification had already been overwritten
  // cost the full wait before being dispatched blind.  Once this task fell
  // behind, every stale byte still in the driver's ring paid that price in turn,
  // so the backlog drained slower than a busy bus refilled it.  That is the
  // green-wire desync seen after a boot-time stall: a long run of single bytes
  // logged one per timeout period, and a length byte read from the middle of a
  // frame large enough to trip the get_packet() capacity clamp.
  //
  // Nothing is lost by waiting for the notification first.  The green-wire UART
  // buffers whatever arrives regardless, so the notification governs only *when*
  // this task drains, never *whether* it does.  A notification carrying no
  // recognised header still falls through to a read, exactly as the SE path
  // does: the notification is a clock, not a filter.
  //
  // Costs one green byte per notification for traffic that matches no header
  // (dispatchDebug).  Notifications arrive several times a second from the F6 /
  // F8 / F9 / FA / FB dispatchers, so a backlog still clears far faster than the
  // one-byte-per-timeout crawl it replaces.
  int monitor_task_sync_impl(uint8_t *buf, uint32_t &val) {
    // Bounded rather than portMAX_DELAY: monitor_rx_task can only observe
    // stop_requested between calls, and VistaBus::stop() can no longer wake
    // this task with a green-wire byte now that it parks on a notification
    // instead of a read.  See kMonitorSyncNotifyWaitMs.
    if (xTaskNotifyWait(0xFFFFFFFF, 0xFFFFFFFF, &val, pdMS_TO_TICKS(kMonitorSyncNotifyWaitMs)) != pdPASS) {
      // No poll dispatched for half a second, so nothing is in flight and
      // anything still buffered here is orphaned -- a green-wire reply whose
      // yellow-wire poll never produced a notification.  Every dispatcher
      // notifies only once the poll's checksum validates (and dispatchF6
      // only for a non-zero address), so a corrupted poll leaves its reply
      // behind with no context to pair it with.
      //
      // That matters because each notification now consumes exactly one
      // message: an extra one left in the ring offsets every read after it
      // by one exchange, permanently.  The old read-first order shed such
      // strays by consuming them blind through dispatchDebug; this is the
      // equivalent, done at the one moment it is provably safe.  On a busy
      // bus the timeout never fires, so this costs nothing in normal
      // operation.
      uart_flush(this->vistabus_.ext_uart_num);
      return 0;
    }

    return uart_read_bytes(this->vistabus_.ext_uart_num, buf, 1, pdMS_TO_TICKS(kMonitorSyncReadWaitMs));
  }

  // Respond to a 0xFA expander poll in-line for timing.
  //
  // The panel's response window is too narrow to round-trip through the receive
  // queue.  Handled types:
  //   0xF1 — zone-state request: dequeue one DeviceMsg and reply with current
  //           zone state encoded as address/sequence/zone-bits/checksum.
  //   0xF7 — status/supervision poll: reply with the stored zone-status bits
  //           (open/closed) and per-zone EOL supervision bits.
  void quick_decodeFA_impl(const char *cbuf) {
    char type = cbuf[4];
    char seq = cbuf[3];
    char lcbuf[5];
    bool valid_address = false;
    uint8_t address = 0;
    uint8_t exp_sequence = (seq == 0x20 || seq == 0x21 ? 0x34 : 0x31);

    for (int index = 1; index <= 5; index++) {
      if (cbuf[2] == 0x01 << index) {
        address = 6 + index;
        valid_address = true;
        break;
      }
    }

    if (vistabus_.emulated_expanders.size() && valid_address)  // check if any emulated expanders are present
    {
      VistaBus::EmulatedExpander *expander = vistabus_.getExpander(address);
      if (expander != nullptr) {
        if (type == 0xF1) {
          // Zone-state request: consume one pending DeviceMsg and reply.
          //
          // Blocking wait: the panel must be given time to respond
          // within this exchange.  In normal operation it returns
          // immediately — this is the grant for a nudge we requested,
          // and rx_tx_task only nudges when the queue is already
          // non-empty (see the deviceMsgQueue notes in vista_bus.h).
          //
          // Only reply if a message was actually dequeued: on a timeout,
          // DeviceMsg's member initialisers would otherwise produce a
          // well-formed but meaningless zone-0 / no-fault frame.
          DeviceMsg expMsg{};
          if (xQueueReceive(vistabus_.deviceMsgQueue, &expMsg, pdMS_TO_TICKS(25)) == pdPASS) {
            lcbuf[0] = address;
            lcbuf[1] = exp_sequence;
            uint8_t z = expMsg.source & 0x07;
            lcbuf[2] = z ? 0 : 0x01;
            lcbuf[3] = (z << 5) ^ (0x10 * expMsg.msg);
            lcbuf[4] = calc_chksum_two(lcbuf, 0, 4);
            uart_write_bytes(vistabus_.uart_num, lcbuf, 5);
          }
        } else if (type == 0xF7) {
          // Fault / supervision poll: reply with stored bitmasks.
          lcbuf[0] = 0xF0;
          lcbuf[1] = exp_sequence;
          lcbuf[2] = expander->zone_status_bits;
          lcbuf[3] = expander->supervision_bits;
          lcbuf[4] = calc_chksum_two(lcbuf, 0, 4);
          uart_write_bytes(vistabus_.uart_num, lcbuf, 5);
        }
      }
    }
  }

  // Read the remainder of a 0xFA expander poll frame, validate its checksum,
  // notify the monitor task of the packet type, and trigger an in-line expander
  // response if EXP emulation is active.  Always enqueues a ReceivedPacket.
  void dispatchFA_impl() {
    uint8_t data[kFAMessageLength + 1];
    ReceivedPacket received_packet{};
    received_packet.type = 0;
    received_packet.payload[0] = 0xFA;
    uint8_t length = kFAMessageLength;

    int rxBytes = this->get_packet_event(&received_packet, data, 1, length - 1, vistabus_.uart_num,
                                         pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue, sizeof(data));
    bool chk = valid_chksum(received_packet.payload, 0, rxBytes + 1);
    if (chk) {
      uint32_t val = 0xFA << 16 | (received_packet.payload[2] << 8) | received_packet.payload[4];
      if (vistabus_.monitor_rx_task_Handle != nullptr)
        xTaskNotify(vistabus_.monitor_rx_task_Handle, val, eSetValueWithOverwrite);
      if (vistabus_.EXPemulation)
        this->quick_decodeFA(received_packet.payload);
      received_packet.source = 0xFA;
    } else
      received_packet.source = 0xCF;

    xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
  }

 private:
  uint8_t last_sequence = 0;
  uint8_t last_address = 99;
};
