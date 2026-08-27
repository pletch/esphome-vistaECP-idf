// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

/*
 * VistaECP — base class for Ademco ECP (Extended Communications Protocol) wire-format handling.
 *
 * The ECP bus uses a master-slave polling model: the panel broadcasts a "poll" frame every
 * ~250 ms, identifying one keypad address at a time.  A device that has data to send must
 * first assert a "pulse mark" on the bus during the poll window to claim a transmit slot,
 * then wait for the panel to respond with an F6 ACK before sending the actual payload.
 *
 * This class implements:
 *   - The ISR glue needed to synchronise the pulse-mark timing with the panel's poll clock.
 *   - Low-level helpers for assembling / disassembling ECP frames.
 *   - Dispatch handlers for each panel command type (0xF2–0xFB).
 *   - Extension dispatch handlers that read the same data from the monitor UART.
 *
 * Concrete subclasses (Vista20P, VistaSE) provide handle_UART_events(), check_send_Q(),
 * monitor_task_sync(), and dispatchFA() which depend on panel-model-specific details.
 */

#include "helper_structs.h"
#include "helper_funcs.h"
#include "constants.h"
#include "vista_bus.h"

class VistaECP {
 public:
  VistaECP(VistaBus &vistabus);

  // --- Transmit state flags ---
  // pulse_mark_time  – esp_timer timestamp at which the last pulse mark was sent.
  //                    Used by track_write_attempts() to detect a missed ACK.
  int64_t pulse_mark_time = 0;

  // pulse_marked – true once mark_pulse() has asserted the address pulse on the bus.
  //               Cleared when the panel's F6 ACK is received or after 10 failures.
  bool pulse_marked = false;

  // req_to_send  – true when an outgoing SendPacket is armed and waiting for an F6
  //               ACK window.  Cleared by dispatchF6() on successful ACK or by
  //               track_write_attempts() after too many failures.
  bool req_to_send = false;

  // is_2400      – true when the subclass has detected a 2400-series panel (uses
  //               additional 0xFA / 0xFB message types).
  bool is_2400 = false;

  // legacy_programmode – true when the panel is currently in program mode (SE-series).
  //                      Changes the F8 decode path to receive the single 33-byte status packet
  //                      used in program mode instead of the normal 4-byte parts.
  bool legacy_programmode = false;

  // --- Frame assembly helpers ---

  // Read up to 'len' bytes from the UART event queue (blocking for up to 'timeout'
  // ms per byte), then append them to received_packet starting at 'start'.
  // 'rxbuf_cap' is the capacity of rxbuf; 'len' is clamped to it so a panel-
  // supplied length byte cannot overrun the caller's buffer.  Pass sizeof(rxbuf).
  // Returns the number of bytes actually read.
  int get_packet_event(struct ReceivedPacket *received_packet, uint8_t *rxbuf, int start, int len, uart_port_t uart_num,
                       int timeout, QueueHandle_t queue, int rxbuf_cap);

  // Blocking variant that calls uart_read_bytes() directly (no event queue).
  // Used when the UART event queue is not available (monitor port, extension decodes).
  // 'rxbuf_cap' has the same meaning as in get_packet_event().
  int get_packet(struct ReceivedPacket *received_packet, uint8_t *rxbuf, int start, int len, uart_port_t uart_num,
                 int timeout, int rxbuf_cap);

  // Encode a SendPacket into ECP wire format and write it to the UART.
  // Prepends the sequence number and length byte, translates ASCII key codes
  // to ECP numeric values (if type==1), and appends a two's-complement checksum.
  int keypad_write(uart_port_t uart_n, const SendPacket &pkt_to_send);

  // Drain the UART event queue into rxbuf, reading at most 'len' bytes.
  // Handles UART_DATA and UART_BREAK events; ignores other event types.
  int uart_read_bytes_event(uart_port_t uart_num, uint8_t *rxbuf, int len, int timeout, QueueHandle_t queue);

  // --- Primary UART dispatch handlers (called from VistaBus::rx_tx_task) ---

  // 0xF2 — Keypad poll.  Reads the length byte and full payload, validates the
  //        checksum, and enqueues a ReceivedPacket (source 0xF2 or 0xCF on error).
  void dispatch_f2();

  // 0xF6 — Keypad address ACK / poll response.
  //        If the ACK address matches our keypad address and req_to_send is set,
  //        transmits the pending payload via keypad_write() and waits for the
  //        panel's echo/ACK byte.  Otherwise drains the trailing bytes silently.
  void dispatch_f6(const SendPacket &pkt_to_send);

  // 0xF7 — Long-Range Radio (LRR) packet.  Fixed-length frame; checksum validated.
  void dispatch_f7();

  // 0xF8 — Unknown purpose in normal mode.  In legacy program mode, carries status
  //        data as a single 33-byte packet rather than the 4-byte parts used otherwise.
  //        Reads the two-byte header (address + length), then the body, validates
  //        the checksum, and notifies the monitor task if active.
  void dispatch_f8();

  // 0xF9 — RF receiver poll.  Same header structure as F8.  When LRR emulation
  //        is enabled, quick_decodeF9() sends a hardware-timed response directly.
  void dispatch_f9();

  // 0xFB — RF receiver data / supervision packet (2400-series panels).
  //        When RF emulation is active, quick_decodeFB() replies inline to meet
  //        the panel's strict timing window.
  void dispatch_fb();

  // SE-series legacy status packet.  Temporarily switches the UART to 2400 baud
  // to collect the 4-byte body, then restores 4800 baud.  Sets legacy_programmode
  // from the data byte if applicable.
  void dispatch_legacy_status_packet(uint8_t header);

  // Enqueue a single-byte debug packet so the application can log unknown headers.
  // 'type' distinguishes primary (0) vs. monitor (1) UART source.
  void dispatch_debug(uint8_t header, uint8_t type);

  // Called every loop iteration from rx_tx_task.  Increments ack_failures if a
  // pulse mark was sent more than 1.2 s ago without receiving an F6 ACK.
  // Abandons the send after 10 consecutive failures.
  void track_write_attempts();

  // --- Monitor UART extension dispatch handlers (called from VistaBus::monitor_rx_task) ---
  // These read the same packet types from the expansion (TX/green) wire via the
  // secondary UART.  'val' carries the header bytes accumulated by monitor_task_sync();
  // 'header' is the most recently received byte (may need alignment correction).

  // 0xF6 extension: keypad-to-panel payload from an expander.
  void dispatch_ext_f6(uint32_t val, uint8_t header);

  // 0xF8 extension: unknown purpose (debug log only).
  void dispatch_ext_f8(uint32_t val, uint8_t header);

  // 0xF9 extension: RF receiver response read from the expansion bus.
  void dispatch_ext_f9(uint32_t val, uint8_t header);

  // 0xFA extension: zone-expander or RF data from 2400-series expansion bus.
  //                 'legacy' selects the address decode path.
  void dispatch_ext_fa(uint32_t val, uint8_t header, bool legacy);

  // 0xFB extension: RF zone message or poll response from expansion bus.
  void dispatch_ext_fb(uint32_t val, uint8_t header);

 protected:
  VistaBus &vistabus_;  // Back-reference to the owning bus for UART / queue access.
  static constexpr const char *TAG = "vista-ecp";

  // Send the ECP address pulse mark for 'address' on 'uartNum'.
  // The pulse consists of 1–3 bytes whose bits encode the address as a one-hot
  // bitmask (active-low).  GPIO edge interrupts on the RX pin are used via
  // xTaskNotifyWait() to synchronise each byte with the panel's clock edges.
  void mark_pulse_(int uart_num, uint8_t address);

  // GPIO ISR handler — fires on any edge of the RX pin.
  // Reads the current GPIO level and sends it to the waiting task via
  // xTaskNotifyFromISR() so mark_pulse() can gate each pulse byte precisely.
  static void IRAM_ATTR gpio_isr_handler(void *args);

  // ESP timer ISR handler — fires when the transmit-window timer expires.
  // Sends 0xFFFFFFFF to unblock a task parked in xTaskNotifyWait().
  static void IRAM_ATTR timer_isr_handler(void *task_handle);

 private:
  // Timing-sensitive inline response to an F9 poll when LRR emulation is active.
  // Must run synchronously (not via the receiveQueue) to meet the panel's ACK window.
  void quick_decode_f9_(const char *cbuf);

  // Timing-sensitive inline response to an FB poll when RF emulation is active.
  // Pulls the pending RF message from deviceMsgQueue and encodes the reply.
  // Non-blocking: the poll is the panel's grant for a nudge we requested, so
  // the message is already queued.  See deviceMsgQueue notes in vista_bus.h.
  void quick_decode_fb_(const char *cbuf);

  // Counter for consecutive F6 ACK misses after a pulse mark; reset on success.
  uint8_t ack_failures_ = 0;
};
