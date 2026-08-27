// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "ecp_protocol.h"
#include "vista_bus.h"

VistaECP::VistaECP(VistaBus &vistabus) : vistabus_(vistabus) {}

// ---------------------------------------------------------------------------
// ISR handlers
// ---------------------------------------------------------------------------

// GPIO edge ISR — called on any rising or falling edge of the RX pin.
// Reads the current pin level and forwards it to the waiting FreeRTOS task
// via xTaskNotifyFromISR().  mark_pulse() uses these notifications to time
// each address-pulse byte with the panel's clock.
// The IRAM_ATTR on the declaration in ecp_protocol.h places the function in
// IRAM so it can execute even when flash cache is disabled during a write.
// It belongs on the declaration only: ESP-IDF's IRAM_ATTR embeds __COUNTER__
// in the section name, so repeating it here would name a second, different
// section for the same function.
void VistaECP::gpio_isr_handler(void *args) {
  GpioTaskArgs *taskargs = (GpioTaskArgs *) args;
  BaseType_t x_higher_priority_task_woken;
  x_higher_priority_task_woken = pdFALSE;
  int val = gpio_get_level(static_cast<gpio_num_t>(taskargs->pin));
  // eSetValueWithOverwrite: the task only needs the most recent edge, not a history.
  xTaskNotifyFromISR(taskargs->task_handle, val, eSetValueWithOverwrite, &x_higher_priority_task_woken);
  portYIELD_FROM_ISR(x_higher_priority_task_woken);
}

// Timer ISR — fires when the transmit-window timer expires.
// Sends 0xFFFFFFFF as a sentinel to unblock a task waiting in xTaskNotifyWait(),
// signalling that the time window has elapsed regardless of any GPIO edges.
void VistaECP::timer_isr_handler(void *task_handle) {
  TaskHandle_t th = (TaskHandle_t) task_handle;
  BaseType_t x_higher_priority_task_woken;
  x_higher_priority_task_woken = pdFALSE;
  xTaskNotifyFromISR(th, 0xFFFFFFFF, eSetValueWithOverwrite, &x_higher_priority_task_woken);
  portYIELD_FROM_ISR(x_higher_priority_task_woken);
}

// ---------------------------------------------------------------------------
// UART helpers
// ---------------------------------------------------------------------------

// Read up to 'len' bytes from the UART by waiting on the UART ISR event queue.
// Each UART_DATA event yields exactly one byte.  UART_BREAK events (framing
// errors on the ECP bus) are silently discarded; the ECP bus uses break
// signalling for inter-frame gaps and they are not data.
// Returns the number of bytes actually read (may be less than 'len' on timeout).
int VistaECP::uart_read_bytes_event(uart_port_t uart_num, uint8_t *rxbuf, int len, int timeout, QueueHandle_t queue) {
  uart_event_t event;
  int bytes = 0;
  while (bytes < len) {
    if (!(xQueueReceive(queue, (void *) &event, timeout) == pdPASS))
      break;
    switch (event.type) {
      case UART_DATA:
        uart_read_bytes(uart_num, &rxbuf[bytes], 1, 0);
        bytes++;
        break;
      case UART_BREAK:
        // Break condition — normal inter-frame gap on the ECP bus; ignore.
        break;
      default:
        break;
    }
  }
  return bytes;
}

// Wait for the next rising edge that clocks a mark-pulse byte onto the bus.
//
// Returns false when no edge arrived: either xTaskNotifyWait() timed out, or the
// transmit-window timer fired its sentinel.  Both cases used to be discarded and
// the byte written regardless, which put it on a shared open-collector bus at a
// moment the panel had not opened.  That is harmless for address < 8, where
// bytes 1 and 2 are zero and skipped anyway, but a real collision for higher
// addresses on panels that emit only one rising edge after the write window --
// the 4140XMPT2 called out in mark_pulse() below is exactly such a panel.
//
// The notified value is the pin level sampled in gpio_isr_handler(), and it is
// deliberately NOT used to filter further.  The interrupt is armed POSEDGE for
// the whole of this sequence, so every GPIO notification here is already a
// rising edge; treating a level that read back 0 (because the ISR sampled after
// a short pulse had fallen again) as "no edge" would reject a genuine one.  Only
// the timer sentinel, which is unambiguous, is rejected.
static bool mark_pulse_edge_arrived(uint32_t timeout_ms) {
  // Matches the value sent by VistaECP::timer_isr_handler().  Only the SE
  // protocol arms that timer, but mark_pulse() is shared by both.
  constexpr uint32_t k_tx_window_timer_sentinel = 0xFFFFFFFF;

  uint32_t notified = 0;
  if (xTaskNotifyWait(0, 0xFFFFFFFF, &notified, pdMS_TO_TICKS(timeout_ms)) != pdPASS)
    return false;
  return notified != k_tx_window_timer_sentinel;
}

// Send the ECP address pulse mark for 'address' on the given UART.
//
// The ECP addressing scheme encodes the keypad address as a one-hot bitmask
// spread across up to three bytes (covering addresses 0–23):
//   byte 0 → addresses  0– 7  (bit position = address % 8, active-low)
//   byte 1 → addresses  8–15  (0xFF means "not in this byte")
//   byte 2 → addresses 16–23
//
// The panel drives the bus low for ~13 ms before each poll window, then
// releases it.  The rising edges are used as a clock: each byte of the pulse
// mark must be sent in a separate rising-edge window.  xTaskNotifyWait()
// blocks until the GPIO ISR signals an edge (or times out), ensuring each
// byte is sent at the correct moment.
//
// Parity is temporarily disabled for the pulse bytes because the bit pattern
// (active-low one-hot) is not valid ECP data and would produce parity errors.
void VistaECP::mark_pulse_(int uart_num, uint8_t address) {
  const uart_port_t port = static_cast<uart_port_t>(uart_num);

  // Ensure any preceding uart_write_bytes() call (e.g. quick_decodeFB response)
  // has fully clocked out of the TX FIFO before we change the parity setting.
  // A parity change mid-transmission corrupts the remaining bytes in the FIFO,
  // causing the monitor UART to read them as 0xFF (parity-error substitution).
  uart_wait_tx_done(port, pdMS_TO_TICKS(30));

  uart_set_parity(port, UART_PARITY_DISABLE);

  char snd_data[3];
  if (address < 8) {
    // Address fits in the first byte; subsequent bytes are 0 (no mark needed).
    snd_data[0] = ~(0x01 << (address & 0x07));  // active-low bit for this address
    snd_data[1] = 0;
    snd_data[2] = 0;
  } else if (address < 17) {
    // Address falls in the second byte range; first byte is 0xFF (all clear).
    snd_data[0] = 0xFF;
    snd_data[1] = ~(0x01 << (address & 0x07));
    snd_data[2] = 0;
  } else {
    // Address falls in the third byte range.
    snd_data[0] = 0xFF;
    snd_data[1] = 0xFF;
    snd_data[2] = ~(0x01 << (address & 0x07));
  }

  // Timeouts as originally tuned: a wider window for the panel to release the
  // bus after the 13 ms low, then one edge period for each follow-on byte.
  constexpr uint32_t k_first_edge_wait_ms = 8;
  constexpr uint32_t k_next_edge_wait_ms = 4;

  // Written as a lambda so every exit below shares the one parity-restore path
  // at the bottom; an early return here must never leave parity disabled.
  auto send_pulse_bytes = [&]() {
    // Wait for the first rising edge (panel releases the bus after the 13 ms
    // low), then send the first pulse byte.  No edge means the panel never
    // opened a window, so there is nothing to write into at all.
    if (!mark_pulse_edge_arrived(k_first_edge_wait_ms)) {
      ESP_LOGD(TAG,
               "mark_pulse: no rising edge within %ums for address %u; "
               "pulse abandoned",
               static_cast<unsigned>(k_first_edge_wait_ms), static_cast<unsigned>(address));
      return;
    }
    uart_write_bytes(port, &snd_data[0], 1);

    // Nothing further to send.  Returning now instead of blocking through two
    // more edge windows matters for two reasons: this runs on the
    // highest-priority task, which is not servicing UART events while it
    // waits; and parity is a single frame-format setting governing RX as well
    // as TX, so every millisecond spent here is a millisecond an inbound 8E2
    // byte would be mis-framed.  This is the common case (address < 8).
    if (snd_data[1] == 0 && snd_data[2] == 0)
      return;

    // Wait for the second rising edge and send the second byte (if needed).
    // Note: older panels (e.g. 4140XMPT2) only produce one rising edge after
    // the 13 ms low and will not trigger this second window.
    if (!mark_pulse_edge_arrived(k_next_edge_wait_ms))
      return;
    if (snd_data[1] != 0)
      uart_write_bytes(port, &snd_data[1], 1);

    if (snd_data[2] == 0)
      return;

    // Wait for the third rising edge and send the third byte.
    if (!mark_pulse_edge_arrived(k_next_edge_wait_ms))
      return;
    uart_write_bytes(port, &snd_data[2], 1);
  };

  send_pulse_bytes();

  // Mirror of the guard at the top of this function, and for the same reason.
  // Restoring parity while the last pulse byte is still in the TX FIFO changes
  // the frame format mid-byte, corrupting a byte the hardware had already
  // begun clocking out.
  uart_wait_tx_done(port, pdMS_TO_TICKS(30));

  uart_set_parity(port, UART_PARITY_EVEN);
}

// Copy rx_bytes of rxbuf into received_packet->payload at offset 'start', clamped
// to the payload buffer so neither the copy nor the trailing null terminator can
// overrun it.  The terminator is written only when a free byte remains — a binary
// frame that exactly fills the buffer (e.g. a full 48-byte F7) is consumed via
// 'size', not as a C-string, so the missing terminator is harmless.
// Returns the number of bytes actually stored.
static int store_packet_payload(struct ReceivedPacket *received_packet, const uint8_t *rxbuf, int start, int rx_bytes) {
  const int cap = static_cast<int>(sizeof(received_packet->payload));
  if (start < 0)
    start = 0;
  if (start > cap)
    start = cap;
  if (rx_bytes < 0)
    rx_bytes = 0;
  if (start + rx_bytes > cap)
    rx_bytes = cap - start;  // never copy past the buffer

  memcpy(received_packet->payload + start, rxbuf, static_cast<size_t>(rx_bytes));

  const int total = start + rx_bytes;
  if (total < cap)
    received_packet->payload[total] = '\0';  // terminate only if room
  received_packet->size = total;
  return rx_bytes;
}

// Clamp a requested read length to the capacity of the caller's rxbuf.
//
// Several call sites derive 'len' straight from a panel-supplied length byte
// (dispatchF8, dispatchF9, dispatch_extF6).  store_packet_payload() clamps the
// copy into ReceivedPacket::payload, but the UART read fills rxbuf *first* —
// so without this the raw length byte overruns the caller's stack array.
// The clamp lives here rather than at each call site so it cannot drift.
static int clamp_read_len(int len, int rxbuf_cap, const char *who) {
  if (len < 0)
    len = 0;
  if (len > rxbuf_cap) {
    ESP_LOGW("vista-ecp", "%s: requested length %d exceeds buffer capacity %d; clamping.", who, len, rxbuf_cap);
    len = rxbuf_cap;
  }
  return len;
}

// Append up to 'len' bytes (event-queue driven) to received_packet starting at
// offset 'start', then null-terminate the payload and update the size field.
// 'rxbuf_cap' is the capacity of rxbuf in bytes; 'len' is clamped to it.
// Returns the number of bytes read from the UART in this call.
int VistaECP::get_packet_event(struct ReceivedPacket *received_packet, uint8_t *rxbuf, int start, int len,
                               uart_port_t uart_num, int timeout, QueueHandle_t queue, int rxbuf_cap) {
  len = clamp_read_len(len, rxbuf_cap, "get_packet_event");
  int rx_bytes = uart_read_bytes_event(uart_num, rxbuf, len, timeout, queue);
  rx_bytes = store_packet_payload(received_packet, rxbuf, start, rx_bytes);
  return rx_bytes;
}

// Same as get_packet_event but uses the blocking uart_read_bytes() API instead
// of the event queue.  Used for the monitor (RX-only) UART and extension decodes
// where no event queue is wired up.
int VistaECP::get_packet(struct ReceivedPacket *received_packet, uint8_t *rxbuf, int start, int len,
                         uart_port_t uart_num, int timeout, int rxbuf_cap) {
  len = clamp_read_len(len, rxbuf_cap, "get_packet");
  int rx_bytes = uart_read_bytes(uart_num, rxbuf, len, timeout);
  rx_bytes = store_packet_payload(received_packet, rxbuf, start, rx_bytes);
  return rx_bytes;
}

// ---------------------------------------------------------------------------
// keypad_write — ECP frame encoder
// ---------------------------------------------------------------------------

// Build the ECP wire-format frame for pkt_to_send and write it to uart_n.
//
// ECP frame layout:
//   [0]      sequence number (identifies which send this is; panel echoes it as ACK)
//   [1]      payload length + 1 (includes the length byte itself in the count)
//   [2..N]   payload bytes
//   [N+1]    checksum = ~(sum of all preceding bytes) + 1  (two's complement)
//
// When type == 1 (normal write), ASCII key characters are translated to the
// ECP numeric codes the panel expects:
//   '0'–'9'  (0x30–0x39) → 0x00–0x09
//   '#'      (0x23)       → 0x0B
//   '*'      (0x2A)       → 0x0A
//   'F'      (0x46)       → 0x0C  (function key)
//   'M'      (0x4D)       → 0x0D
//   'P'      (0x50)       → 0x0E
//   'G'      (0x47)       → 0x0F
//   'A'–'D'  (0x41–0x44)  → 0x1C–0x1F  (subtract 0x25)
//
// When type == 0 (writedirect), bytes are already encoded; copy them verbatim.
int VistaECP::keypad_write(const uart_port_t uart_n, const SendPacket &pkt_to_send) {
  // The wire frame is size + 3 bytes (sequence, length, payload, checksum), so
  // the buffer must be the payload capacity plus that 3-byte overhead.  Clamp
  // size to the payload capacity defensively: a size that fit SendPacket.payload
  // could still have overrun a 24-byte frame buffer (off-by-the-overhead).
  const int max_payload = static_cast<int>(sizeof(pkt_to_send.payload));
  int size = pkt_to_send.size;
  if (size < 0)
    size = 0;
  if (size > max_payload)
    size = max_payload;

  char outbuffer[sizeof(pkt_to_send.payload) + 3];
  memset(outbuffer, '\0', sizeof(outbuffer));
  outbuffer[0] = pkt_to_send.sequence;
  outbuffer[1] = size + 1;  // length field includes itself

  uint8_t chksum = 0;
  chksum += outbuffer[0] + outbuffer[1];

  for (int i = 2; i < size + 2; i++) {
    if (pkt_to_send.type == 0)  // write direct as hex — no translation needed
    {
      outbuffer[i] = pkt_to_send.payload[i - 2];
    } else  // translate from ASCII keypad characters to ECP codes
    {
      if (pkt_to_send.payload[i - 2] >= 0x30 && pkt_to_send.payload[i - 2] <= 0x39) {
        outbuffer[i] = (pkt_to_send.payload[i - 2] - 0x30);  // '0'–'9' → 0–9
      } else if (pkt_to_send.payload[i - 2] == 0x23) {
        outbuffer[i] = 0x0B;  // '#'
      } else if (pkt_to_send.payload[i - 2] == 0x2A) {
        outbuffer[i] = 0x0A;  // '*'
      } else if (pkt_to_send.payload[i - 2] == 0x46) {
        outbuffer[i] = 0x0C;  // 'F' — function key
      } else if (pkt_to_send.payload[i - 2] == 0x4D) {
        outbuffer[i] = 0x0D;  // 'M'
      } else if (pkt_to_send.payload[i - 2] == 0x50) {
        outbuffer[i] = 0x0E;  // 'P'
      } else if (pkt_to_send.payload[i - 2] == 0x47) {
        outbuffer[i] = 0x0F;  // 'G'
      } else if (pkt_to_send.payload[i - 2] >= 0x41 && pkt_to_send.payload[i - 2] <= 0x44) {
        outbuffer[i] = (pkt_to_send.payload[i - 2] - 0x25);  // 'A'–'D' → 0x1C–0x1F
      }
    }
    chksum += outbuffer[i];
  }

  // Two's-complement checksum: ~sum + 1, so that all bytes (including the
  // checksum itself) sum to 0x00 modulo 256.
  outbuffer[size + 2] = ~chksum + 1;

  return uart_write_bytes(uart_n, outbuffer, size + 3);
}

// ---------------------------------------------------------------------------
// Primary UART dispatch handlers
// ---------------------------------------------------------------------------

// 0xF2 — Keypad poll packet.
// The panel sends 0xF2 followed by a length byte, then 'length' payload bytes,
// ending with a checksum.  All bytes are read via the event queue for consistency.
// The checksum is validated; source is set to 0xCF (corrupt) on mismatch.
void VistaECP::dispatch_f2() {
  uint8_t data[K_RX_BUF_SIZE + 1];
  ReceivedPacket received_packet{};
  received_packet.type = 0;
  received_packet.payload[0] = 0xF2;

  // Read the length byte (second byte of the frame).
  int rx_bytes =
      this->uart_read_bytes_event(vistabus_.uart_num, data, 1, pdMS_TO_TICKS(K_UART_DELAY), vistabus_.uartevtQueue);
  received_packet.payload[1] = data[0];

  // Read 'length' body bytes into the payload starting at offset 2.
  // payload is char (signed): read the length byte through uint8_t so a value
  // above 0x7F is not seen as negative.  get_packet_event clamps it to sizeof(data).
  rx_bytes = this->get_packet_event(
      &received_packet, data, 2, static_cast<int>(static_cast<uint8_t>(received_packet.payload[1])), vistabus_.uart_num,
      pdMS_TO_TICKS(K_UART_DELAY), vistabus_.uartevtQueue, sizeof(data));

  const bool f2_ok = valid_chksum(received_packet.payload, 0, rx_bytes + 2);
  received_packet.source = f2_ok ? 0xF2 : 0xCF;  // 0xCF = checksum failure marker

  xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
}

// 0xF6 — Keypad address poll / ACK.
//
// The panel uses 0xF6 to poll one keypad address per cycle.  The byte
// immediately following 0xF6 is the addressed keypad's bus address.
//
// If a monitor task is running, the (header, address) pair is forwarded to it
// so it can synchronise reading the same exchange from the expansion bus.
//
// If our address matches and req_to_send is set:
//   1. Transmit the pending payload via keypad_write().
//   2. Wait up to 100 ms for the panel's echo/ACK byte (the sequence number).
//   3. Clear req_to_send and pulse_marked on success.
//
// If the ACK is for a different device, drain the trailing response byte.
//
// Address values 1, 2, 5, 6 represent the primary keypad and are reported
// with source 0xF2; all others use source 0xF6.
void VistaECP::dispatch_f6(const SendPacket &pkt_to_send) {
  uint8_t data[4];
  ReceivedPacket received_packet{};
  received_packet.type = 0;
  received_packet.payload[0] = 0xF6;

  // Read the address byte.
  int rx_bytes =
      this->uart_read_bytes_event(vistabus_.uart_num, data, 1, pdMS_TO_TICKS(K_UART_DELAY), vistabus_.uartevtQueue);

  // Notify the monitor task so it can read the corresponding expansion-bus packet.
  if (data[0] != 0 && vistabus_.monitor_rx_task_Handle != nullptr) {
    uint32_t val = 0xF6 << 8 | data[0];
    xTaskNotify(vistabus_.monitor_rx_task_Handle, val, eSetValueWithOverwrite);
  }

  received_packet.payload[1] = data[0];
  received_packet.size = 2;

  // Classify the source address: primary keypad vs. expansion device.
  if (received_packet.payload[1] == 1 || received_packet.payload[1] == 2 || received_packet.payload[1] == 5 ||
      received_packet.payload[1] == 6) {
    received_packet.source = 0xF2;  // primary keypad address range
  } else {
    received_packet.source = 0xF6;
  }

  xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(20));

  // Flush the trailing zero byte the panel appends after the address byte.
  this->uart_read_bytes_event(vistabus_.uart_num, &data[1], 1, pdMS_TO_TICKS(K_UART_DELAY), vistabus_.uartevtQueue);

  if (this->req_to_send && data[0] == pkt_to_send.keypadaddress)  // ACK was for us
  {
    // The panel has granted us a transmit window — send the payload now.
    data[rx_bytes] = 0;
    this->keypad_write(vistabus_.uart_num, pkt_to_send);

    // uart_write_bytes() returns once the frame is queued, not once it is on
    // the wire.  The panel cannot echo anything until our transmission has
    // finished clocking out, so that has to be waited for separately -- a
    // single combined timeout has to cover both, and sizing it for the echo
    // alone expires before we have even stopped talking.  A SendPacket
    // payload is up to 24 bytes, so a wire frame runs to 27 bytes, and at
    // 4800 baud 8E2 (12 bits per byte, 2.5 ms) that is 67.5 ms of
    // transmission before the panel gets its turn.
    constexpr uint32_t k_write_tx_done_wait_ms = 80;
    uart_wait_tx_done(vistabus_.uart_num, pdMS_TO_TICKS(k_write_tx_done_wait_ms));

    // Now wait for the panel to echo our sequence number as confirmation.
    //
    // Measured from the end of our transmission rather than the start of it,
    // so this only has to cover the panel's turnaround.  Splitting the two
    // also keeps the total proportional to the frame actually sent: a 9-byte
    // AUI write clears its TX wait in about 22 ms instead of reserving the
    // worst case.  Both halves block rx_tx_task, which is what drains the
    // UART event queue, so the total still wants to stay inside what that
    // queue can absorb -- see K_UART_EVENT_QUEUE_DEPTH.
    constexpr uint32_t k_sequence_echo_wait_ms = 30;
    rx_bytes = this->get_packet_event(&received_packet, data, 0, 1, vistabus_.uart_num,
                                      pdMS_TO_TICKS(k_sequence_echo_wait_ms), vistabus_.uartevtQueue, sizeof(data));
    if (rx_bytes) {
      if (data[0] == pkt_to_send.sequence) {
        // Panel echoed the correct sequence number — send acknowledged.
        this->req_to_send = false;
        this->pulse_marked = false;
#ifdef DEBUG_LOG
        xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(20));
#endif
      }

      if (this->req_to_send) {
        // Received a byte but it wasn't our sequence number.
        ESP_LOGW(TAG, "Did not find expected byte in response of %d bytes.", rx_bytes);
        this->req_to_send = false;
      }
    } else {
      // No response at all — the panel may have missed our transmission.
      ESP_LOGW(TAG, "Did not receive any response bytes from panel.");
      this->req_to_send = false;
    }
  } else  // ACK was for another device — drain its response byte.
  {
    rx_bytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 1, pdMS_TO_TICKS(50), vistabus_.uartevtQueue);
#ifdef DEBUG_LOG
    if (rx_bytes)  // single response byte from the addressed device
    {
      received_packet.payload[0] = data[0];
      received_packet.size = 1;
      xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
    }
#endif
  }
}

// 0xF7 — Long-Range Radio (LRR) message.
// Fixed-length frame (K_F7_MESSAGE_LENGTH bytes including the header byte already
// consumed).  Checksum validated; source set to 0xCF on failure.
void VistaECP::dispatch_f7() {
  uint8_t data[K_F7_MESSAGE_LENGTH - 1];
  ReceivedPacket received_packet{};
  received_packet.type = 0;
  received_packet.payload[0] = 0xF7;

  int rx_bytes = this->get_packet_event(&received_packet, data, 1, K_F7_MESSAGE_LENGTH - 1, vistabus_.uart_num,
                                        pdMS_TO_TICKS(K_UART_DELAY), vistabus_.uartevtQueue, sizeof(data));
  const bool f7_ok = valid_chksum(received_packet.payload, 0, rx_bytes + 1);
  received_packet.source = f7_ok ? 0xF7 : 0xCF;

  xQueueSend(vistabus_.receiveQueue, &received_packet, 0);
}

// 0xF8 — Zone-expander poll (or legacy program-mode data).
//
// Normal path (non-legacy-programmode):
//   [0]   0xF8  (already consumed by the dispatcher)
//   [1]   expander address
//   [2]   body length
//   [3..] body bytes  + checksum
//   On checksum pass, the (header, address) pair is forwarded to the monitor task.
//
// Legacy program mode:
//   The panel streams raw configuration data.  Reads a fixed 32-byte block
//   and marks source 0xDD (raw data).
void VistaECP::dispatch_f8() {
  uint8_t data[K_RX_BUF_SIZE + 1];
  ReceivedPacket received_packet{};
  received_packet.type = 0;
  received_packet.payload[0] = 0xF8;
  int rx_bytes = 0;

  // ToDo: Consider if checksum is needed in following section
  if (this->legacy_programmode) {
    // In program mode the panel sends status as a single 33-byte packet rather
    // than the normal 4-byte parts used outside of program mode.
    received_packet.source = 0xDD;
    rx_bytes = this->get_packet_event(&received_packet, data, 1, 32, vistabus_.uart_num, pdMS_TO_TICKS(K_UART_DELAY),
                                      vistabus_.uartevtQueue, sizeof(data));
  } else {
    // Normal: read the address byte and length byte, then the body.
    rx_bytes =
        this->uart_read_bytes_event(vistabus_.uart_num, data, 2, pdMS_TO_TICKS(K_UART_DELAY), vistabus_.uartevtQueue);
    if (rx_bytes == 2) {
      received_packet.payload[1] = data[0];  // expander address
      received_packet.payload[2] = data[1];  // body length
      const int want = static_cast<int>(data[1]);
      rx_bytes = this->get_packet_event(&received_packet, data, 3, want, vistabus_.uart_num,
                                        pdMS_TO_TICKS(K_UART_DELAY), vistabus_.uartevtQueue, sizeof(data));
      const bool f8_ok = valid_chksum(received_packet.payload, 0, rx_bytes + 3);
      if (f8_ok) {
        // Forward to monitor task for expansion-bus correlation.
        uint32_t val = 0xF8 << 8 | received_packet.payload[1];
        if (vistabus_.monitor_rx_task_Handle != nullptr)
          xTaskNotify(vistabus_.monitor_rx_task_Handle, val, eSetValueWithOverwrite);
      }
    }
  }

  xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
}

// 0xF9 — RF receiver poll.
// Frame: [0xF9][address][length][body bytes][checksum]
//
// On checksum pass, a 32-bit summary (0xF9 | address | first-body-byte) is
// forwarded to the monitor task, and quick_decodeF9() is called inline if
// LRR emulation is active (timing is too tight to go via the receive queue).
//
// After forwarding to the receive queue, the two trailing bytes (zero pad +
// panel status) are drained.
void VistaECP::dispatch_f9() {
  uint8_t data[K_RX_BUF_SIZE + 1];
  ReceivedPacket received_packet{};
  received_packet.type = 0;
  received_packet.payload[0] = 0xF9;

  // Read the address byte and length byte.
  int rx_bytes =
      this->uart_read_bytes_event(vistabus_.uart_num, data, 2, pdMS_TO_TICKS(K_UART_DELAY), vistabus_.uartevtQueue);
  if (rx_bytes == 2) {
    received_packet.payload[1] = data[0];  // RF receiver address
    received_packet.payload[2] = data[1];  // body length

    const int want = static_cast<int>(data[1]);
    rx_bytes = this->get_packet_event(&received_packet, data, 3, want, vistabus_.uart_num, pdMS_TO_TICKS(K_UART_DELAY),
                                      vistabus_.uartevtQueue, sizeof(data));

    const bool f9_ok = valid_chksum(received_packet.payload, 0, rx_bytes + 3);
    if (f9_ok) {
      received_packet.source = 0xF9;

      // Pack address and first body byte into a 32-bit value for the monitor task.
      uint32_t val = 0xF9 << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
      if (vistabus_.monitor_rx_task_Handle != nullptr)
        xTaskNotify(vistabus_.monitor_rx_task_Handle, val, eSetValueWithOverwrite);

      // If we are emulating an LRR module, respond immediately — the panel
      // has a very short ACK window that cannot wait for the receive queue.
      if (vistabus_.LRRemulation)
        this->quick_decode_f9_(received_packet.payload);
    } else {
      received_packet.source = 0xCF;
    }

    xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));

    if (received_packet.source == 0xF9) {
      // Drain the two trailing bytes: a zero pad byte and the panel status byte.
      rx_bytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 2, pdMS_TO_TICKS(30), vistabus_.uartevtQueue);
#ifdef DEBUG_LOG
      if (rx_bytes)  // log the panel status byte (data[1]; data[0] is the zero pad)
      {
        received_packet.payload[0] = data[1];
        received_packet.size = 1;
        xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
      }
#endif
    }
  }
}

// Timing-critical inline response for LRR emulation during an F9 poll.
// The panel expects a reply within the same poll cycle; going via the receive
// queue would introduce too much latency.
//
// cbuf[3] == 0x53  — LRR status request: respond with a 6-byte status frame.
//   The response address byte is cbuf[1] | 0x40.  The checksum is encoded in
//   the upper nibble of the last byte using a 0x0F-complement formula plus 0x09.
//
// cbuf[3] == 0x48 / 0x52 / 0x58  — Echo the address byte back as ACK.
void VistaECP::quick_decode_f9_(const char *cbuf) {
  // For timing, must handle 0xF9 packet here if emulating rather than through queues in vistaalarm process.
  char response[6];
  if (cbuf[3] == 0x53)
  // F9 83 02 53 2F
  // C3 04 00 60 00 D9
  {
    response[0] = cbuf[1] + 0x40;
    response[1] = 0x04;
    response[2] = 0;
    response[3] = 0;
    response[4] = 0;
    response[5] = (((0x0F - (response[0] >> 4)) & 0x0F) << 4) | 0x09;
    uart_write_bytes(vistabus_.uart_num, response, 6);
  } else if (cbuf[3] == 0x48 || cbuf[3] == 0x52 || cbuf[3] == 0x58) {
    uart_write_bytes(vistabus_.uart_num, &cbuf[1], 1);  // echo address byte as ACK
  }
}

// 0xFB — RF receiver data / supervision packet (2400-series panels).
// Frame: [0xFB][address][sequence][type][body...][checksum]
//
// cbuf[3] (type) values:
//   0xF1        — panel is requesting RF zone data (quick_decodeFB responds inline)
//   0x60 / 0x81 / 0x82 — supervision query (quick_decodeFB responds with receiver ID)
//
// On checksum pass the monitor task is notified and source is set to 0xFB.
// On failure, source is 0xCF.
void VistaECP::dispatch_fb() {
  uint8_t data[K_FB_MESSAGE_LENGTH + 1];
  ReceivedPacket received_packet{};
  received_packet.type = 0;
  received_packet.payload[0] = 0xFB;

  int rx_bytes = this->get_packet_event(&received_packet, data, 1, K_FB_MESSAGE_LENGTH - 1, vistabus_.uart_num,
                                        pdMS_TO_TICKS(K_UART_DELAY), vistabus_.uartevtQueue, sizeof(data));
  const bool fb_ok = valid_chksum(received_packet.payload, 0, rx_bytes + 1);
  if (fb_ok) {
    // Notify the monitor task with a summary of this FB packet.
    uint32_t val = 0xFB << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
    if (vistabus_.monitor_rx_task_Handle != nullptr)
      xTaskNotify(vistabus_.monitor_rx_task_Handle, val, eSetValueWithOverwrite);

    // If emulating an RF receiver, respond to the panel now while we still
    // have the bus — the reply window closes before we could process this
    // via the receive queue.
    if (vistabus_.RFRemulation)
      this->quick_decode_fb_(received_packet.payload);

    received_packet.source = 0xFB;
  } else {
    received_packet.source = 0xCF;
  }

  xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
}

// Timing-critical inline response for RF receiver emulation during an FB poll.
//
// type == 0xF1 (data request):
//   Pull the pending RF sensor message from deviceMsgQueue — this poll is the
//   panel's grant for a nudge we requested, so the head is that message.
//   Build a 7-byte response containing the RF receiver address, a sequence
//   number, the 20-bit RF serial packed into 3 bytes (high byte bit 7 set as
//   valid-sensor flag, bits 3:0 = serial bits 19:16), the message byte, and
//   a two's-complement checksum.  The sequence byte alternates: 0x20→0x54, else 0x51.
//
// type == 0x60 / 0x81 / 0x82 (supervision query):
//   Reply with a 4-byte "I'm here" frame containing the receiver address,
//   sequence, receiver model ID (0x05 = 5881ENH), and checksum.
void VistaECP::quick_decode_fb_(const char *cbuf) {
  // For timing, must handle 0xFB packet here if emulating rather than through queues in vistaalarm process.
  uint8_t type = cbuf[3];
  // 0xF1 - response to request, 0x80 - retry, 0x60 or 0x81 supervision, 0x82 supervision w/ type response
  if (type == 0xF1) {
    // A 0xF1 data request is the panel's grant for a nudge we requested, and
    // rx_tx_task only nudges when the queue is already non-empty (see the
    // deviceMsgQueue notes in vista_bus.h), so in normal operation this
    // dequeue succeeds immediately.  The wait is a margin against the
    // producer still finishing its enqueue, not a routine stall.
    //
    // Keep that margin short.  This runs inline on rx_tx_task -- the
    // highest-priority task -- with the panel's reply window open, so every
    // millisecond spent blocked here is a millisecond of UART events not
    // being serviced.  The timeout is insurance against an unanticipated
    // race, not a wait the design expects to use: if nothing is queued,
    // waiting longer cannot conjure a message that was never posted.  It was
    // 100 ms, which made any such race cost 40 byte times of bus blindness.
    // Matches the expander F1 paths in the protocol headers.
    constexpr uint32_t k_f1_grant_queue_wait_ms = 25;
    // Only reply if a message was actually dequeued: on a timeout, DeviceMsg's
    // member initialisers would otherwise produce a well-formed but
    // meaningless serial-0 / no-fault frame to the panel.
    DeviceMsg rf_msg{};
    if (xQueueReceive(vistabus_.deviceMsgQueue, &rf_msg, pdMS_TO_TICKS(k_f1_grant_queue_wait_ms)) == pdPASS) {
      uint8_t seq = cbuf[2];
      char lcbuf[7];
      uint8_t exp_seq = (seq == 0x20 ? 0x54 : 0x51);  // alternate sequence numbers
      lcbuf[0] = vistabus_.emulated_rf_receiver.address;
      lcbuf[1] = exp_seq;
      lcbuf[2] = rf_msg.source >> 16 | 0x80;  // serial bits 19:16 in low nibble; bit 7 = valid-sensor flag
      lcbuf[3] = rf_msg.source >> 8 & 0xFF;   // serial bits 15:8
      lcbuf[4] = rf_msg.source & 0xFF;        // serial bits 7:0
      lcbuf[5] = rf_msg.msg;                  // fault/loop status byte
      lcbuf[6] = calc_chksum_two(lcbuf, 0, 6);
      uart_write_bytes(vistabus_.uart_num, lcbuf, 7);
    }
  } else if (type == 0x60 || type == 0x81 || type == 0x82) {
    // Supervision query — respond with receiver presence and model ID.
    uint8_t seq = cbuf[2];
    char lcbuf[4];
    uint8_t exp_seq = (seq == 0x20 ? 0x24 : 0x21);
    lcbuf[0] = vistabus_.emulated_rf_receiver.address;
    lcbuf[1] = exp_seq;
    lcbuf[2] = 0x05;  // 5881ENL = 3, 5881ENH = 5
    lcbuf[3] = calc_chksum_two(lcbuf, 0, 3);
    uart_write_bytes(vistabus_.uart_num, lcbuf, 4);
  }
}

// Legacy SE-series status packet (for panels that transmit at 2400 baud).
// The UART baud rate is temporarily lowered to 2400 for the body bytes, then
// restored to 4800.  The source is always 0xDD (raw / legacy data).
// Bit 6 of the third body byte signals entry into / exit from program mode.
void VistaECP::dispatch_legacy_status_packet(uint8_t header) {
  uint8_t data[6];
  ReceivedPacket received_packet{};
  received_packet.type = 0;
  received_packet.payload[0] = header;

  vistabus_.set_baud_fast(false);
  int rx_bytes = this->get_packet_event(&received_packet, data, 1, 4, vistabus_.uart_num, pdMS_TO_TICKS(K_UART_DELAY),
                                        vistabus_.uartevtQueue, sizeof(data));
  received_packet.source = 0xDD;
  vistabus_.set_baud_fast(true);

  xQueueSend(vistabus_.receiveQueue, &received_packet, 0);

  // Update the program-mode flag from bit 6 of the third body byte (data[2]).
  // Exclude the 0xFE and 0xFF status bytes which don't carry this information.
  if ((header != 0xFE && header != 0xFF) && rx_bytes == 4) {
    this->legacy_programmode = data[2] & 0x40;
  }
}

// Enqueue a single-byte debug/unknown-header packet so the application can log it.
// type 0 = from the primary UART; type 1 = from the monitor (expansion-bus) UART.
void VistaECP::dispatch_debug(uint8_t header, uint8_t type) {
  ReceivedPacket received_packet{};
  received_packet.type = type;
  received_packet.payload[0] = header;
  received_packet.size = 1;
  xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(20));
}

// ---------------------------------------------------------------------------
// track_write_attempts — ACK failure watchdog
// ---------------------------------------------------------------------------

// Called every iteration of rx_tx_task.  Monitors whether a pending send has
// been acknowledged within the expected 1.2 s window after the pulse mark.
//
// Failure path:
//   If req_to_send is set, a pulse mark was sent (pulse_marked), and more than
//   1.2 s has elapsed since pulse_mark_time, the panel did not send an F6 ACK.
//   pulse_marked is cleared so the next loop iteration can attempt another mark.
//   After 10 consecutive failures the send is abandoned and req_to_send is cleared.
//
// Success path:
//   Once req_to_send goes false (cleared by dispatchF6 on success) the failure
//   counter is reset for the next send operation.
void VistaECP::track_write_attempts() {
  if (this->req_to_send && this->pulse_marked &&
      (esp_timer_get_time() - this->pulse_mark_time > K_PULSE_ACK_TIMEOUT_US)) {
    this->ack_failures_++;
    this->pulse_marked = false;  // allow another pulse mark attempt
  }

  if (this->ack_failures_ == 10) {
    ESP_LOGW(TAG, "Failure to receive F6 ACK after 10 successive pulse marks.  Giving up.");
    this->req_to_send = false;
    this->ack_failures_ = 0;
  }

  if (!this->req_to_send) {
    this->ack_failures_ = 0;
    this->pulse_marked = false;
  }
}

// ---------------------------------------------------------------------------
// Monitor UART (expansion-bus) extension dispatch handlers
// ---------------------------------------------------------------------------
// These functions read data from the secondary (RX-only) UART that is wired
// to the expansion bus (green / TX wire).  The monitor_rx_task accumulates
// received bytes into a 32-bit shift register 'val' and dispatches here when
// it detects a known header pattern.
//
// The first byte from the secondary UART ('header') may be slightly offset
// from the header byte seen on the primary UART, so each handler syncs to
// the expected byte by reading ahead up to 2–3 times if needed.

// 0xF6 extension — reads the keypad payload from the expansion bus.
// 'val' carries [0xF6][address] in its upper 16 bits.
// The lower 4 bits of the address byte are compared with the already-read
// 'header' byte to detect and discard any misaligned leading bytes.
// Source is classified the same way as the primary dispatchF6.
void VistaECP::dispatch_ext_f6(uint32_t val, uint8_t header) {
  uint8_t data[48];
  ReceivedPacket rcvd_ext_pkt{};
  rcvd_ext_pkt.type = 1;  // 1 = from monitor/expansion UART
  int rx_bytes = 0;
  uint8_t n = 0;
  data[0] = header;

  // Discard up to 3 leading mark bytes until the address nibble matches.
  while ((data[0] & 0x0F) != static_cast<uint8_t>(val & 0x0F) && n < 3) {
    rx_bytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(K_UART_DELAY));
    n++;
  }

  rcvd_ext_pkt.payload[0] = data[0];  // confirmed address byte

  // Read the length byte, then the body.
  rx_bytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(150));
  rcvd_ext_pkt.payload[1] = data[0];  // body length

  this->get_packet(&rcvd_ext_pkt, data, 2, static_cast<int>(static_cast<uint8_t>(rcvd_ext_pkt.payload[1])),
                   vistabus_.ext_uart_num, pdMS_TO_TICKS(150), sizeof(data));

  // Classify source the same way as the primary F6 handler.
  if (static_cast<uint8_t>(val) == 1 || static_cast<uint8_t>(val) == 2 || static_cast<uint8_t>(val) == 5 ||
      static_cast<uint8_t>(val) == 6) {
    rcvd_ext_pkt.source = 0xF2;
  } else {
    rcvd_ext_pkt.source = 0xF6;
  }

  xQueueSend(vistabus_.receiveQueue, &rcvd_ext_pkt, pdMS_TO_TICKS(0));
}

// 0xF8 extension — zone-expander response from the expansion bus.
// Only logged in DEBUG_LOG mode; the body content is not currently decoded.
void VistaECP::dispatch_ext_f8(uint32_t val, uint8_t header) {
  uint8_t data[2];
  ReceivedPacket rcvd_ext_pkt{};
  rcvd_ext_pkt.type = 1;
  int rx_bytes = 0;
  uint8_t n = 0;
  data[0] = header;

  // Sync: discard up to 2 bytes until the address matches val's low byte.
  while ((data[0]) != static_cast<uint8_t>(val) && n < 2) {
    rx_bytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(K_UART_DELAY));
    n++;
  }

#ifdef DEBUG_LOG
  rcvd_ext_pkt.payload[0] = data[0];
  rcvd_ext_pkt.size = 1;
  xQueueSend(vistabus_.receiveQueue, &rcvd_ext_pkt, pdMS_TO_TICKS(0));
#endif
}

// 0xF9 extension — RF receiver response on the expansion bus.
// The response byte may be either the address (val >> 8) or address | 0x40
// (some panels set bit 6 in the response).
// When the command byte is 0x53, a 5-byte reply is captured and forwarded.
void VistaECP::dispatch_ext_f9(uint32_t val, uint8_t header) {
  uint8_t data[7];
  ReceivedPacket rcvd_ext_pkt{};
  rcvd_ext_pkt.type = 1;
  int rx_bytes = 0;
  uint8_t n = 0;
  data[0] = header;

  // Accept either the plain address or the address with bit 6 set.
  uint8_t mb = static_cast<uint8_t>(val >> 8) + 0x40;
  while (data[0] != mb && data[0] != static_cast<uint8_t>(val >> 8) && n < 2) {
    rx_bytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(K_UART_DELAY));
    n++;
  }

  rcvd_ext_pkt.payload[0] = data[0];
  rcvd_ext_pkt.source = 0xF9;

  if (static_cast<uint8_t>(val) == 0x53) {
    // Command 0x53: capture the 5-byte reply body and forward to the application.
    this->get_packet(&rcvd_ext_pkt, data, 1, 5, vistabus_.ext_uart_num, pdMS_TO_TICKS(25), sizeof(data));
    xQueueSend(vistabus_.receiveQueue, &rcvd_ext_pkt, pdMS_TO_TICKS(0));
  }
#ifdef DEBUG_LOG
  else {
    // Other commands: log the single-byte address for debug purposes.
    rcvd_ext_pkt.size = 1;
    xQueueSend(vistabus_.receiveQueue, &rcvd_ext_pkt, pdMS_TO_TICKS(0));
  }
#endif
}

// 0xFA extension — zone-expander or RF data from the expansion bus (2400-series).
//
// val layout:  [0xFA][bit-mask byte][type byte]
//   bit-mask byte: one-hot bit encoding for expander addresses 7–11 (non-legacy)
//   type byte:     0xF1 = incoming zone fault data; anything else = RF / other data
//
// When type == 0xF1:
//   Identify the target expander address from the bit-mask, sync to that address
//   byte on the expansion bus, read 3 body bytes, and forward with source 0xFA.
//
// Otherwise:
//   Sync to 0xF0, read 5 body bytes, and forward with source 0xFA.
void VistaECP::dispatch_ext_fa(uint32_t val, uint8_t header, bool legacy) {
  uint8_t data[7];
  ReceivedPacket rcvd_ext_pkt{};
  rcvd_ext_pkt.type = 1;
  int rx_bytes = 0;
  data[0] = header;

  if (static_cast<uint8_t>(val) == 0xF1)  // Incoming zone data from Expander
  {
    // Map the one-hot bit-mask to the expander's keybus address.
    uint8_t req_addr = 99;  // sentinel for "unknown"
    if (legacy) {
      req_addr = 1;  // legacy SE protocol uses a single fixed address
    } else {
      switch (static_cast<uint8_t>(val >> 8)) {
        case 0x02:
          req_addr = 7;
          break;  // zones  9–16
        case 0x04:
          req_addr = 8;
          break;  // zones 17–24
        case 0x08:
          req_addr = 9;
          break;  // zones 25–32
        case 0x10:
          req_addr = 10;
          break;  // zones 33–40
        case 0x20:
          req_addr = 11;
          break;  // zones 41–48
        default:
          break;
      }
    }
    ESP_LOGE(TAG, "dispatch_extFA: decoded expander address %d from bitmask 0x%02X", req_addr,
             static_cast<uint8_t>(val >> 8));

    // Sync to the expander's address byte on the expansion bus.
    uint8_t n = 0;
    while (data[0] != req_addr && n < 2) {
      rx_bytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(K_UART_DELAY));
      n++;
    }

    rcvd_ext_pkt.payload[0] = data[0];
    int res =
        this->get_packet(&rcvd_ext_pkt, data, 1, 3, vistabus_.ext_uart_num, pdMS_TO_TICKS(K_UART_DELAY), sizeof(data));
    if (res > 0) {
      rcvd_ext_pkt.source = 0xFA;
      xQueueSend(vistabus_.receiveQueue, &rcvd_ext_pkt, pdMS_TO_TICKS(10));
    }
  } else {
    // Non-zone-data FA packet: sync to 0xF0 start byte, then read 5-byte body.
    uint8_t n = 0;
    while (data[0] != 0xF0 && n < 2) {
      rx_bytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(K_UART_DELAY));
      n++;
    }

    rcvd_ext_pkt.payload[0] = data[0];
    this->get_packet(&rcvd_ext_pkt, data, 1, 5, vistabus_.ext_uart_num, pdMS_TO_TICKS(50), sizeof(data));
    rcvd_ext_pkt.source = 0xFA;
    xQueueSend(vistabus_.receiveQueue, &rcvd_ext_pkt, pdMS_TO_TICKS(0));
  }
}

// 0xFB extension — RF receiver data / poll response from the expansion bus.
//
// val layout:  [0xFB][bit-mask byte][type byte]
//   bit-mask byte: one-hot bit encoding for RF receiver addresses 0–7 (mapped
//                  to bus addresses via the switch table below)
//   type byte:     0xF1 = zone data; anything else = poll response
//
// Sync to the resolved receiver address byte, then read the frame body.
// source is always 0xFB on success.
void VistaECP::dispatch_ext_fb(uint32_t val, uint8_t header) {
  uint8_t data[K_RF_ZONE_MESSAGE_LENGTH + 1];
  ReceivedPacket rcvd_ext_pkt{};
  rcvd_ext_pkt.type = 1;
  int rx_bytes = 0;
  uint8_t n = 0;
  data[0] = header;

  // Map the one-hot bit-mask in val[15:8] to a receiver bus address.
  uint8_t req_addr = 99;
  switch (static_cast<uint8_t>(val >> 8)) {
    case 0x01:
      req_addr = 7;
      break;
    case 0x02:
      req_addr = 0;
      break;
    case 0x04:
      req_addr = 1;
      break;
    case 0x08:
      req_addr = 2;
      break;
    case 0x10:
      req_addr = 3;
      break;
    case 0x20:
      req_addr = 4;
      break;
    case 0x40:
      req_addr = 5;
      break;
    case 0x80:
      req_addr = 6;
      break;
    default:
      break;
  }

  // Sync to the expected receiver address byte.
  while (data[0] != req_addr && n < 2) {
    rx_bytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(K_UART_DELAY));
    n++;
  }

  rcvd_ext_pkt.payload[0] = data[0];

  if ((val & 0xFF) == 0xF1)  // Incoming zone data from RF Receiver
  {
    // Zone data: read the full RF zone message body.
    int res = this->get_packet(&rcvd_ext_pkt, data, 1, K_RF_ZONE_MESSAGE_LENGTH - 1, vistabus_.ext_uart_num,
                               pdMS_TO_TICKS(K_UART_DELAY), sizeof(data));
    if (res > 0) {
      rcvd_ext_pkt.source = 0xFB;
      xQueueSend(vistabus_.receiveQueue, &rcvd_ext_pkt, pdMS_TO_TICKS(20));
    }
  } else  // Response to FB poll command — read 3-byte body.
  {
    this->get_packet(&rcvd_ext_pkt, data, 1, 3, vistabus_.ext_uart_num, pdMS_TO_TICKS(K_UART_DELAY), sizeof(data));
    rcvd_ext_pkt.source = 0xFB;
    xQueueSend(vistabus_.receiveQueue, &rcvd_ext_pkt, pdMS_TO_TICKS(0));
  }
}
