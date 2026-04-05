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

VistaECP::VistaECP(VistaBus& vistabus) : vistabus_(vistabus) {}


// ---------------------------------------------------------------------------
// ISR handlers
// ---------------------------------------------------------------------------

// GPIO edge ISR — called on any rising or falling edge of the RX pin.
// Reads the current pin level and forwards it to the waiting FreeRTOS task
// via xTaskNotifyFromISR().  mark_pulse() uses these notifications to time
// each address-pulse byte with the panel's clock.
// IRAM_ATTR ensures the function is placed in IRAM so it can execute even
// when flash cache is disabled during a write.
void IRAM_ATTR VistaECP::gpio_isr_handler(void * args)
{
    GpioTaskArgs * taskargs = (GpioTaskArgs *) args;
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    int val = gpio_get_level(static_cast<gpio_num_t>(taskargs->pin));
    // eSetValueWithOverwrite: the task only needs the most recent edge, not a history.
    xTaskNotifyFromISR(taskargs->task_handle, val, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// Timer ISR — fires when the transmit-window timer expires.
// Sends 0xFFFFFFFF as a sentinel to unblock a task waiting in xTaskNotifyWait(),
// signalling that the time window has elapsed regardless of any GPIO edges.
void IRAM_ATTR VistaECP::timer_isr_handler(void * task_handle)
{
    TaskHandle_t th = (TaskHandle_t) task_handle;
    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(th, 0xFFFFFFFF, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


// ---------------------------------------------------------------------------
// UART helpers
// ---------------------------------------------------------------------------

// Read up to 'len' bytes from the UART by waiting on the UART ISR event queue.
// Each UART_DATA event yields exactly one byte.  UART_BREAK events (framing
// errors on the ECP bus) are silently discarded; the ECP bus uses break
// signalling for inter-frame gaps and they are not data.
// Returns the number of bytes actually read (may be less than 'len' on timeout).
int VistaECP::uart_read_bytes_event(uart_port_t uart_num, uint8_t * rxbuf, int len, int timeout, QueueHandle_t queue)
{
    uart_event_t event;
    int bytes = 0;
    while (bytes < len)
    {
        if (!(xQueueReceive(queue, (void *)&event, timeout) == pdPASS))
            break;
        switch (event.type)
        {
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
void VistaECP::mark_pulse(int uartNum, uint8_t address)
{
    uart_set_parity(static_cast<uart_port_t>(uartNum), UART_PARITY_DISABLE);

    char snd_data[3];
    if (address < 8)
    {
        // Address fits in the first byte; subsequent bytes are 0 (no mark needed).
        snd_data[0] = ~(0x01 << (address & 0x07)); // active-low bit for this address
        snd_data[1] = 0;
        snd_data[2] = 0;
    }
    else if (address < 17)
    {
        // Address falls in the second byte range; first byte is 0xFF (all clear).
        snd_data[0] = 0xFF;
        snd_data[1] = ~(0x01 << (address & 0x07));
        snd_data[2] = 0;
    }
    else
    {
        // Address falls in the third byte range.
        snd_data[0] = 0xFF;
        snd_data[1] = 0xFF;
        snd_data[2] = ~(0x01 << (address & 0x07));
    }

    // Wait for the first rising edge (panel releases the bus after the 13 ms low),
    // then send the first pulse byte.
    xTaskNotifyWait(0, 0xFFFFFFFF, NULL, pdMS_TO_TICKS(8)); // first rising edge
    uart_write_bytes(static_cast<uart_port_t>(uartNum), &snd_data[0], 1);

    // Wait for the second rising edge and send the second byte (if needed).
    // Note: older panels (e.g. 4140XMPT2) only produce one rising edge after
    // the 13 ms low and will not trigger this second window.
    xTaskNotifyWait(0, 0xFFFFFFFF, NULL, pdMS_TO_TICKS(4)); // second rising edge
    if (snd_data[1] != 0)
        uart_write_bytes(static_cast<uart_port_t>(uartNum), &snd_data[1], 1);

    // Wait for the third rising edge and send the third byte (if needed).
    xTaskNotifyWait(0, 0xFFFFFFFF, NULL, pdMS_TO_TICKS(4)); // third rising edge
    if (snd_data[2] != 0)
        uart_write_bytes(static_cast<uart_port_t>(uartNum), &snd_data[2], 1);

    uart_set_parity(static_cast<uart_port_t>(uartNum), UART_PARITY_EVEN);
}

// Append up to 'len' bytes (event-queue driven) to received_packet starting at
// offset 'start', then null-terminate the payload and update the size field.
// Returns the number of bytes read from the UART in this call.
int VistaECP::get_packet_event(struct ReceivedPacket * received_packet, uint8_t * rxbuf,
        int start, int len, uart_port_t uart_num, int timeout, QueueHandle_t queue)
{
    const int rxBytes = uart_read_bytes_event(uart_num, rxbuf, len, timeout, queue);
    memcpy(received_packet->payload + start, rxbuf, rxBytes);
    received_packet->payload[rxBytes + start] = '\0';
    received_packet->size = rxBytes + start;
    return rxBytes;
}

// Same as get_packet_event but uses the blocking uart_read_bytes() API instead
// of the event queue.  Used for the monitor (RX-only) UART and extension decodes
// where no event queue is wired up.
int VistaECP::get_packet(struct ReceivedPacket * received_packet, uint8_t * rxbuf,
        int start, int len, uart_port_t uart_num, int timeout)
{
    const int rxBytes = uart_read_bytes(uart_num, rxbuf, len, timeout);
    memcpy(received_packet->payload + start, rxbuf, rxBytes);
    received_packet->payload[rxBytes + start] = '\0';
    received_packet->size = rxBytes + start;
    return rxBytes;
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
int VistaECP::keypad_write(const uart_port_t uart_n, const SendPacket &pkt_to_send)
{
    char outbuffer[24];
    memset(outbuffer, '\0', sizeof(outbuffer));
    outbuffer[0] = pkt_to_send.sequence;
    outbuffer[1] = pkt_to_send.size + 1; // length field includes itself

    uint8_t chksum = 0;
    chksum += outbuffer[0] + outbuffer[1];

    for (int i = 2; i < pkt_to_send.size + 2; i++)
    {
        if (pkt_to_send.type == 0) // write direct as hex — no translation needed
        {
            outbuffer[i] = pkt_to_send.payload[i - 2];
        }
        else // translate from ASCII keypad characters to ECP codes
        {
            if (pkt_to_send.payload[i-2] >= 0x30 && pkt_to_send.payload[i-2] <= 0x39)
                outbuffer[i] = (pkt_to_send.payload[i-2] - 0x30);  // '0'–'9' → 0–9
            else if (pkt_to_send.payload[i-2] == 0x23)
                outbuffer[i] = 0x0B;  // '#'
            else if (pkt_to_send.payload[i-2] == 0x2A)
                outbuffer[i] = 0x0A;  // '*'
            else if (pkt_to_send.payload[i-2] == 0x46)
                outbuffer[i] = 0x0C;  // 'F' — function key
            else if (pkt_to_send.payload[i-2] == 0x4D)
                outbuffer[i] = 0x0D;  // 'M'
            else if (pkt_to_send.payload[i-2] == 0x50)
                outbuffer[i] = 0x0E;  // 'P'
            else if (pkt_to_send.payload[i-2] == 0x47)
                outbuffer[i] = 0x0F;  // 'G'
            else if (pkt_to_send.payload[i-2] >= 0x41 && pkt_to_send.payload[i-2] <= 0x44)
                outbuffer[i] = (pkt_to_send.payload[i-2] - 0x25);  // 'A'–'D' → 0x1C–0x1F
        }
        chksum += outbuffer[i];
    }

    // Two's-complement checksum: ~sum + 1, so that all bytes (including the
    // checksum itself) sum to 0x00 modulo 256.
    outbuffer[pkt_to_send.size + 2] = ~chksum + 1;

    return uart_write_bytes(uart_n, outbuffer, pkt_to_send.size + 3);
}


// ---------------------------------------------------------------------------
// Primary UART dispatch handlers
// ---------------------------------------------------------------------------

// 0xF2 — Keypad poll packet.
// The panel sends 0xF2 followed by a length byte, then 'length' payload bytes,
// ending with a checksum.  All bytes are read via the event queue for consistency.
// The checksum is validated; source is set to 0xCF (corrupt) on mismatch.
void VistaECP::dispatchF2()
{
    uint8_t data[kRXBufSize+1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF2;

    // Read the length byte (second byte of the frame).
    int rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 1,
                                               pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    received_packet.payload[1] = data[0];

    // Read 'length' body bytes into the payload starting at offset 2.
    rxBytes = this->get_packet_event(&received_packet, data, 2,
                                      static_cast<int>(received_packet.payload[1]),
                                      vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);

    if (valid_chksum(received_packet.payload, 0, rxBytes + 2))
        received_packet.source = 0xF2;
    else
        received_packet.source = 0xCF; // checksum failure marker

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
void VistaECP::dispatchF6(const SendPacket &pkt_to_send)
{
    uint8_t data[4];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF6;

    // Read the address byte.
    int rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 1,
                                               pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);

    // Notify the monitor task so it can read the corresponding expansion-bus packet.
    if (data[0] != 0 && vistabus_.monitor_rx_task_Handle != nullptr)
    {
        uint32_t val = 0xF6 << 8 | data[0];
        xTaskNotify(vistabus_.monitor_rx_task_Handle, val, eSetValueWithOverwrite);
    }

    received_packet.payload[1] = data[0];
    received_packet.size = 2;

    // Classify the source address: primary keypad vs. expansion device.
    if (received_packet.payload[1] == 1 || received_packet.payload[1] == 2 ||
            received_packet.payload[1] == 5 || received_packet.payload[1] == 6)
        received_packet.source = 0xF2; // primary keypad address range
    else
        received_packet.source = 0xF6;

    xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(20));

    // Flush the trailing zero byte the panel appends after the address byte.
    this->uart_read_bytes_event(vistabus_.uart_num, &data[1], 1,
                                 pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);

    if (this->req_to_send && data[0] == pkt_to_send.keypadaddress) // ACK was for us
    {
        // The panel has granted us a transmit window — send the payload now.
        data[rxBytes] = 0;
        this->keypad_write(vistabus_.uart_num, pkt_to_send);

        // Wait for the panel to echo our sequence number as confirmation.
        rxBytes = this->get_packet_event(&received_packet, data, 0, 1,
                                          vistabus_.uart_num, pdMS_TO_TICKS(100), vistabus_.uartevtQueue);
        if (rxBytes)
        {
            if (data[0] == pkt_to_send.sequence)
            {
                // Panel echoed the correct sequence number — send acknowledged.
                this->req_to_send = false;
                this->pulse_marked = false;
#ifdef DEBUG_LOG
                xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(20));
#endif
            }

            if (this->req_to_send)
            {
                // Received a byte but it wasn't our sequence number.
                ESP_LOGW(TAG, "Did not find expected byte in response of %d bytes.", rxBytes);
                this->req_to_send = false;
            }
        }
        else
        {
            // No response at all — the panel may have missed our transmission.
            ESP_LOGW(TAG, "Did not receive any response bytes from panel.");
            this->req_to_send = false;
        }
    }
    else // ACK was for another device — drain its response byte.
    {
        rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 1,
                                               pdMS_TO_TICKS(50), vistabus_.uartevtQueue);
#ifdef DEBUG_LOG
        if (rxBytes) // single response byte from the addressed device
        {
            received_packet.payload[0] = data[0];
            received_packet.size = 1;
            xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
        }
#endif
    }
}

// 0xF7 — Long-Range Radio (LRR) message.
// Fixed-length frame (kF7MessageLength bytes including the header byte already
// consumed).  Checksum validated; source set to 0xCF on failure.
void VistaECP::dispatchF7()
{
    uint8_t data[kF7MessageLength-1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF7;

    int rxBytes = this->get_packet_event(&received_packet, data, 1, kF7MessageLength - 1,
                                          vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    if (valid_chksum(received_packet.payload, 0, rxBytes + 1))
        received_packet.source = 0xF7;
    else
        received_packet.source = 0xCF;

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
void VistaECP::dispatchF8()
{
    uint8_t data[kRXBufSize+1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF8;
    int rxBytes = 0;

    //ToDo: Consider if checksum is needed in following section
    if (this->legacy_programmode)
    {
        // In program mode the panel sends status as a single 33-byte packet rather
        // than the normal 4-byte parts used outside of program mode.
        received_packet.source = 0xDD;
        rxBytes = this->get_packet_event(&received_packet, data, 1, 32,
                                          vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    }
    else
    {
        // Normal: read the address byte and length byte, then the body.
        rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 2,
                                               pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
        if (rxBytes == 2)
        {
            received_packet.payload[1] = data[0]; // expander address
            received_packet.payload[2] = data[1]; // body length
            rxBytes = this->get_packet_event(&received_packet, data, 3,
                                              static_cast<int>(data[1]),
                                              vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
            if (valid_chksum(received_packet.payload, 0, rxBytes + 3))
            {
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
void VistaECP::dispatchF9()
{
    uint8_t data[kRXBufSize+1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xF9;

    // Read the address byte and length byte.
    int rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 2,
                                               pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    if (rxBytes == 2)
    {
        received_packet.payload[1] = data[0]; // RF receiver address
        received_packet.payload[2] = data[1]; // body length

        rxBytes = this->get_packet_event(&received_packet, data, 3,
                                          static_cast<int>(data[1]),
                                          vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);

        if (valid_chksum(received_packet.payload, 0, rxBytes + 3))
        {
            received_packet.source = 0xF9;

            // Pack address and first body byte into a 32-bit value for the monitor task.
            uint32_t val = 0xF9 << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
            if (vistabus_.monitor_rx_task_Handle != nullptr)
                xTaskNotify(vistabus_.monitor_rx_task_Handle, val, eSetValueWithOverwrite);

            // If we are emulating an LRR module, respond immediately — the panel
            // has a very short ACK window that cannot wait for the receive queue.
            if (vistabus_.LRRemulation)
                this->quick_decodeF9(received_packet.payload);
        }
        else
            received_packet.source = 0xCF;

        xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));

        if (received_packet.source == 0xF9)
        {
            // Drain the two trailing bytes: a zero pad byte and the panel status byte.
            rxBytes = this->uart_read_bytes_event(vistabus_.uart_num, data, 2,
                                                   pdMS_TO_TICKS(30), vistabus_.uartevtQueue);
#ifdef DEBUG_LOG
            if (rxBytes) // log the panel status byte (data[1]; data[0] is the zero pad)
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
void VistaECP::quick_decodeF9(const char * cbuf)
{
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
    }
    else if (cbuf[3] == 0x48 || cbuf[3] == 0x52 || cbuf[3] == 0x58)
    {
        uart_write_bytes(vistabus_.uart_num, &cbuf[1], 1); // echo address byte as ACK
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
void VistaECP::dispatchFB()
{
    uint8_t data[kFBMessageLength+1];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = 0xFB;

    int rxBytes = this->get_packet_event(&received_packet, data, 1, kFBMessageLength - 1,
                                          vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    if (valid_chksum(received_packet.payload, 0, rxBytes + 1))
    {
        // Notify the monitor task with a summary of this FB packet.
        uint32_t val = 0xFB << 16 | received_packet.payload[1] << 8 | received_packet.payload[3];
        if (vistabus_.monitor_rx_task_Handle != nullptr)
            xTaskNotify(vistabus_.monitor_rx_task_Handle, val, eSetValueWithOverwrite);

        // If emulating an RF receiver, respond to the panel now while we still
        // have the bus — the reply window closes before we could process this
        // via the receive queue.
        if (vistabus_.RFRemulation)
            this->quick_decodeFB(received_packet.payload);

        received_packet.source = 0xFB;
    }
    else
    {
        received_packet.source = 0xCF;
    }

    xQueueSend(vistabus_.receiveQueue, &received_packet, pdMS_TO_TICKS(0));
}

// Timing-critical inline response for RF receiver emulation during an FB poll.
//
// type == 0xF1 (data request):
//   Pull the next pending RF sensor message from deviceMsgQueue.
//   Build a 7-byte response containing the RF receiver address, a sequence
//   number, the 3-byte RF serial number (MSB set), the message byte, and
//   a two's-complement checksum.  The sequence byte alternates: 0x20→0x54, else 0x51.
//
// type == 0x60 / 0x81 / 0x82 (supervision query):
//   Reply with a 4-byte "I'm here" frame containing the receiver address,
//   sequence, receiver model ID (0x05 = 5881ENH), and checksum.
void VistaECP::quick_decodeFB(const char * cbuf)
{
    // For timing, must handle 0xFB packet here if emulating rather than through queues in vistaalarm process.
    uint8_t type = cbuf[3];
    // 0xF1 - response to request, 0x80 - retry, 0x60 or 0x81 supervision, 0x82 supervision w/ type response
    if (type == 0xF1)
    {
        DeviceMsg rfMsg;
        if (xQueueReceive(vistabus_.deviceMsgQueue, &rfMsg, pdMS_TO_TICKS(100)) == pdPASS)
        {
            uint8_t seq = cbuf[2];
            char lcbuf[7];
            uint8_t exp_seq = (seq == 0x20 ? 0x54 : 0x51); // alternate sequence numbers
            lcbuf[0] = vistabus_.emulated_rf_receiver.address;
            lcbuf[1] = exp_seq;
            lcbuf[2] = rfMsg.source >> 16 | 0x80;   // RF serial number, high byte (MSB set = valid sensor)
            lcbuf[3] = rfMsg.source >> 8 & 0xFF;    // RF serial number, mid byte
            lcbuf[4] = rfMsg.source & 0xFF;         // RF serial number, low byte
            lcbuf[5] = rfMsg.msg;                   // fault/loop status byte
            lcbuf[6] = calc_chksum_two(lcbuf, 0, 6);
            uart_write_bytes(vistabus_.uart_num, lcbuf, 7);
        }
    }
    else if (type == 0x60 || type == 0x81 || type == 0x82)
    {
        // Supervision query — respond with receiver presence and model ID.
        uint8_t seq = cbuf[2];
        char lcbuf[4];
        uint8_t exp_seq = (seq == 0x20 ? 0x24 : 0x21);
        lcbuf[0] = vistabus_.emulated_rf_receiver.address;
        lcbuf[1] = exp_seq;
        lcbuf[2] = 0x05; // 5881ENL = 3, 5881ENH = 5
        lcbuf[3] = calc_chksum_two(lcbuf, 0, 3);
        uart_write_bytes(vistabus_.uart_num, lcbuf, 4);
    }
}

// Legacy SE-series status packet (for panels that transmit at 2400 baud).
// The UART baud rate is temporarily lowered to 2400 for the body bytes, then
// restored to 4800.  The source is always 0xDD (raw / legacy data).
// Bit 6 of the third body byte signals entry into / exit from program mode.
void VistaECP::dispatch_legacyStatusPacket(uint8_t header)
{
    uint8_t data[6];
    ReceivedPacket received_packet;
    received_packet.type = 0;
    received_packet.payload[0] = header;

    uart_set_baudrate(vistabus_.uart_num, 2400);
    int rxBytes = this->get_packet_event(&received_packet, data, 1, 4,
                                          vistabus_.uart_num, pdMS_TO_TICKS(kUartDelay), vistabus_.uartevtQueue);
    received_packet.source = 0xDD;
    uart_set_baudrate(vistabus_.uart_num, 4800);

    xQueueSend(vistabus_.receiveQueue, &received_packet, 0);

    // Update the program-mode flag from bit 6 of the third body byte (data[2]).
    // Exclude the 0xFE and 0xFF status bytes which don't carry this information.
    if ((header != 0xFE && header != 0xFF) && rxBytes == 4)
    {
        this->legacy_programmode = data[2] & 0x40;
    }
}

// Enqueue a single-byte debug/unknown-header packet so the application can log it.
// type 0 = from the primary UART; type 1 = from the monitor (expansion-bus) UART.
void VistaECP::dispatchDebug(uint8_t header, uint8_t type)
{
    ReceivedPacket received_packet;
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
void VistaECP::track_write_attempts()
{
    if (this->req_to_send && this->pulse_marked
            && (esp_timer_get_time() - this->pulse_mark_time > 1200000)) // 1.2 s timeout
    {
        this->ack_failures++;
        this->pulse_marked = false; // allow another pulse mark attempt
    }

    if (this->ack_failures == 10)
    {
        ESP_LOGW(TAG, "Failure to receive F6 ACK after 10 successive pulse marks.  Giving up.");
        this->req_to_send = false;
        this->ack_failures = 0;
    }

    if (!this->req_to_send)
    {
        this->ack_failures = 0;
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
void VistaECP::dispatch_extF6(uint32_t val, uint8_t header)
{
    uint8_t data[48]; //ToDo verify what maximum size is in write service
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1; // 1 = from monitor/expansion UART
    int rxBytes = 0;
    uint8_t n = 0;
    data[0] = header;

    // Discard up to 3 leading mark bytes until the address nibble matches.
    while ((data[0] & 0x0F) != static_cast<uint8_t>(val & 0x0F) && n < 3)
    {
        rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
        n++;
    }

    rcvd_extPkt.payload[0] = data[0]; // confirmed address byte

    // Read the length byte, then the body.
    rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(150));
    rcvd_extPkt.payload[1] = data[0]; // body length

    this->get_packet(&rcvd_extPkt, data, 2, rcvd_extPkt.payload[1],
                     vistabus_.ext_uart_num, pdMS_TO_TICKS(150));

    // Classify source the same way as the primary F6 handler.
    if (static_cast<uint8_t>(val) == 1 || static_cast<uint8_t>(val) == 2
            || static_cast<uint8_t>(val) == 5 || static_cast<uint8_t>(val) == 6)
        rcvd_extPkt.source = 0xF2;
    else
        rcvd_extPkt.source = 0xF6;

    xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(0));
}

// 0xF8 extension — zone-expander response from the expansion bus.
// Only logged in DEBUG_LOG mode; the body content is not currently decoded.
void VistaECP::dispatch_extF8(uint32_t val, uint8_t header)
{
    uint8_t data[2];
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    int rxBytes = 0;
    uint8_t n = 0;
    data[0] = header;

    // Sync: discard up to 2 bytes until the address matches val's low byte.
    while ((data[0]) != static_cast<uint8_t>(val) && n < 2)
    {
        rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
        n++;
    }

#ifdef DEBUG_LOG
    rcvd_extPkt.payload[0] = data[0];
    rcvd_extPkt.size = 1;
    xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(0));
#endif
}

// 0xF9 extension — RF receiver response on the expansion bus.
// The response byte may be either the address (val >> 8) or address | 0x40
// (some panels set bit 6 in the response).
// When the command byte is 0x53, a 5-byte reply is captured and forwarded.
void VistaECP::dispatch_extF9(uint32_t val, uint8_t header)
{
    uint8_t data[7];
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    int rxBytes = 0;
    uint8_t n = 0;
    data[0] = header;

    // Accept either the plain address or the address with bit 6 set.
    uint8_t mb = static_cast<uint8_t>(val >> 8) + 0x40;
    while (data[0] != mb && data[0] != static_cast<uint8_t>(val >> 8) && n < 2)
    {
        rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
        n++;
    }

    rcvd_extPkt.payload[0] = data[0];
    rcvd_extPkt.source = 0xF9;

    if (static_cast<uint8_t>(val) == 0x53)
    {
        // Command 0x53: capture the 5-byte reply body and forward to the application.
        this->get_packet(&rcvd_extPkt, data, 1, 5, vistabus_.ext_uart_num, pdMS_TO_TICKS(25));
        xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(0));
    }
#ifdef DEBUG_LOG
    else
    {
        // Other commands: log the single-byte address for debug purposes.
        rcvd_extPkt.size = 1;
        xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(0));
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
void VistaECP::dispatch_extFA(uint32_t val, uint8_t header, bool legacy)
{
    uint8_t data[7];
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    int rxBytes = 0;
    data[0] = header;

    if (static_cast<uint8_t>(val) == 0xF1) // Incoming zone data from Expander
    {
        // Map the one-hot bit-mask to the expander's keybus address.
        uint8_t req_addr = 99; // sentinel for "unknown"
        if (legacy)
        {
            req_addr = 1; // legacy SE protocol uses a single fixed address
        }
        else
        {
            switch (static_cast<uint8_t>(val >> 8))
            {
                case 0x02: req_addr = 7;  break; // zones  9–16
                case 0x04: req_addr = 8;  break; // zones 17–24
                case 0x08: req_addr = 9;  break; // zones 25–32
                case 0x10: req_addr = 10; break; // zones 33–40
                case 0x20: req_addr = 11; break; // zones 41–48
                default:                  break;
            }
        }

        // Sync to the expander's address byte on the expansion bus.
        uint8_t n = 0;
        while (data[0] != req_addr && n < 2)
        {
            rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
            n++;
        }

        rcvd_extPkt.payload[0] = data[0];
        int res = this->get_packet(&rcvd_extPkt, data, 1, 3,
                                    vistabus_.ext_uart_num, pdMS_TO_TICKS(kUartDelay));
        if (res > 0)
        {
            rcvd_extPkt.source = 0xFA;
            xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(10));
        }
    }
    else
    {
        // Non-zone-data FA packet: sync to 0xF0 start byte, then read 5-byte body.
        uint8_t n = 0;
        while (data[0] != 0xF0 && n < 2)
        {
            rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
            n++;
        }

        rcvd_extPkt.payload[0] = data[0];
        this->get_packet(&rcvd_extPkt, data, 1, 5, vistabus_.ext_uart_num, pdMS_TO_TICKS(50));
        rcvd_extPkt.source = 0xFA;
        xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(0));
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
void VistaECP::dispatch_extFB(uint32_t val, uint8_t header)
{
    uint8_t data[kRFZoneMessageLength+1];
    ReceivedPacket rcvd_extPkt;
    rcvd_extPkt.type = 1;
    int rxBytes = 0;
    uint8_t n = 0;
    data[0] = header;

    // Map the one-hot bit-mask in val[15:8] to a receiver bus address.
    uint8_t req_addr = 99;
    switch (static_cast<uint8_t>(val >> 8))
    {
        case 0x01: req_addr = 7; break;
        case 0x02: req_addr = 0; break;
        case 0x04: req_addr = 1; break;
        case 0x08: req_addr = 2; break;
        case 0x10: req_addr = 3; break;
        case 0x20: req_addr = 4; break;
        case 0x40: req_addr = 5; break;
        case 0x80: req_addr = 6; break;
        default:                 break;
    }

    // Sync to the expected receiver address byte.
    while (data[0] != req_addr && n < 2)
    {
        rxBytes = uart_read_bytes(vistabus_.ext_uart_num, data, 1, pdMS_TO_TICKS(kUartDelay));
        n++;
    }

    rcvd_extPkt.payload[0] = data[0];

    if ((val & 0xFF) == 0xF1) // Incoming zone data from RF Receiver
    {
        // Zone data: read the full RF zone message body.
        int res = this->get_packet(&rcvd_extPkt, data, 1, kRFZoneMessageLength - 1,
                                    vistabus_.ext_uart_num, pdMS_TO_TICKS(kUartDelay));
        if (res > 0)
        {
            rcvd_extPkt.source = 0xFB;
            xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(20));
        }
    }
    else // Response to FB poll command — read 3-byte body.
    {
        this->get_packet(&rcvd_extPkt, data, 1, 3, vistabus_.ext_uart_num, pdMS_TO_TICKS(kUartDelay));
        rcvd_extPkt.source = 0xFB;
        xQueueSend(vistabus_.receiveQueue, &rcvd_extPkt, pdMS_TO_TICKS(0));
    }
}
