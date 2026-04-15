// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf.
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once
#include "esphome/core/defines.h"
#ifdef CC1101_RECEIVER

#include <cstdint>
#include <vector>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------------------------------------------------------------------
// CC1101 sub-GHz RF transceiver SPI driver.
//
// Targets 345 MHz OOK reception for Honeywell 5800-series wireless sensors.
//
// The CC1101 is configured in Asynchronous Serial Mode (PKTCTRL0=0x32).  In
// this mode the FIFO is bypassed entirely: GDO0 (IOCFG0=0x0D) outputs the
// raw demodulated OOK bitstream in real-time — high when carrier present, low
// otherwise.  The ESP32 RMT peripheral measures pulse widths on GDO0 directly.
// honeywell345_parse() converts those widths into chips (~136 µs/chip) and
// performs software Manchester decode + CRC-16 validation.  Hardware Manchester
// is disabled; the DRATE register setting does not affect the async output.
//
// No sync word is used.  The Honeywell preamble is a solid carrier (all-ones),
// which provides no transitions for sync word detection.  Packet boundaries are
// instead found by the CRC-validated sliding Manchester decode in software.
// Noise is rejected by RSSI gating in the receive task (rssi_now()).
//
// Key parameters:
//   Frequency:  ~344.975 MHz  (FREQ2/1/0 = 0x0D/0x44/0xAD = 869549)
//               FREQ_word × 26e6 / 2^16 ≈ 344,975 kHz
//   Modulation: OOK  (MDMCFG2[6:4] = 011), Manchester OFF, no sync word
//   Mode:       Asynchronous Serial (PKTCTRL0[5:4] = 11); FIFO bypassed
//   GDO0:       Raw demodulated OOK output (IOCFG0=0x0D)
//   Channel BW: ~203 kHz  (MDMCFG4=0x87; CHANBW_E=2, CHANBW_M=0)
//               BW = 26e6 / (8 × 4 × 4) ≈ 203 kHz
//
// Reference: rtl_433 src/devices/honeywell.c — OOK_PULSE_MANCHESTER_ZEROBIT
// ---------------------------------------------------------------------------

// CC1101 register addresses
namespace CC1101Reg {
    inline constexpr uint8_t IOCFG2   = 0x00;
    inline constexpr uint8_t IOCFG1   = 0x01;
    inline constexpr uint8_t IOCFG0   = 0x02;
    inline constexpr uint8_t FIFOTHR  = 0x03;
    inline constexpr uint8_t SYNC1    = 0x04;
    inline constexpr uint8_t SYNC0    = 0x05;
    inline constexpr uint8_t PKTLEN   = 0x06;
    inline constexpr uint8_t PKTCTRL1 = 0x07;
    inline constexpr uint8_t PKTCTRL0 = 0x08;
    inline constexpr uint8_t ADDR     = 0x09;
    inline constexpr uint8_t CHANNR   = 0x0A;
    inline constexpr uint8_t FSCTRL1  = 0x0B;
    inline constexpr uint8_t FSCTRL0  = 0x0C;
    inline constexpr uint8_t FREQ2    = 0x0D;
    inline constexpr uint8_t FREQ1    = 0x0E;
    inline constexpr uint8_t FREQ0    = 0x0F;
    inline constexpr uint8_t MDMCFG4  = 0x10;
    inline constexpr uint8_t MDMCFG3  = 0x11;
    inline constexpr uint8_t MDMCFG2  = 0x12;
    inline constexpr uint8_t MDMCFG1  = 0x13;
    inline constexpr uint8_t MDMCFG0  = 0x14;
    inline constexpr uint8_t DEVIATN  = 0x15;
    inline constexpr uint8_t MCSM2    = 0x16;
    inline constexpr uint8_t MCSM1    = 0x17;
    inline constexpr uint8_t MCSM0    = 0x18;
    inline constexpr uint8_t FOCCFG   = 0x19;
    inline constexpr uint8_t BSCFG    = 0x1A;
    inline constexpr uint8_t AGCCTRL2 = 0x1B;
    inline constexpr uint8_t AGCCTRL1 = 0x1C;
    inline constexpr uint8_t AGCCTRL0 = 0x1D;
    inline constexpr uint8_t FREND1   = 0x21;
    inline constexpr uint8_t FREND0   = 0x22;
    inline constexpr uint8_t FSCAL3   = 0x23;
    inline constexpr uint8_t FSCAL2   = 0x24;
    inline constexpr uint8_t FSCAL1   = 0x25;
    inline constexpr uint8_t FSCAL0   = 0x26;
    inline constexpr uint8_t TEST2    = 0x2C;
    inline constexpr uint8_t TEST1    = 0x2D;
    inline constexpr uint8_t TEST0    = 0x2E;
}

// CC1101 status registers (read with burst bit set: addr | 0xC0)
namespace CC1101Status {
    inline constexpr uint8_t PARTNUM   = 0x30;
    inline constexpr uint8_t VERSION   = 0x31;
    inline constexpr uint8_t RSSI      = 0x34;
    inline constexpr uint8_t MARCSTATE = 0x35;
    inline constexpr uint8_t RXBYTES   = 0x3B;  // [7]=RXFIFO_OVERFLOW, [6:0]=bytes
}

// CC1101 command strobes
namespace CC1101Strobe {
    inline constexpr uint8_t SRES  = 0x30;  // Reset
    inline constexpr uint8_t SRX   = 0x34;  // Enable RX
    inline constexpr uint8_t SIDLE = 0x36;  // Enter IDLE state
    inline constexpr uint8_t SFRX  = 0x3A;  // Flush RX FIFO
    inline constexpr uint8_t SNOP  = 0x3D;  // No-op / read status
}

inline constexpr uint8_t CC1101_RXFIFO     = 0xFF;  // burst read RX FIFO
inline constexpr uint8_t CC1101_READ_BURST = 0xC0;  // status register read flag
inline constexpr uint8_t CC1101_READ_BIT   = 0x80;  // single-byte read flag
inline constexpr uint8_t CC1101_WRITE_BIT  = 0x00;

class CC1101 {
public:
    // mosi/miso/sck/csn: SPI bus pin numbers.
    // gdo0: CC1101 GDO0 output pin — raw demodulated OOK output in async serial
    //       mode (IOCFG0=0x0D): high when carrier present, low otherwise.
    CC1101(int mosi, int miso, int sck, int csn, int gdo0);

    // Initialise SPI bus, load register config, enter RX mode.
    // Returns false if the chip does not respond (VERSION register unreadable).
    bool begin();

    // Live RSSI read directly from the RSSI status register, in dBm.
    // Use this before read_packet() to gate on signal strength.
    int8_t rssi_now();

    // Return the GPIO numbers for all SPI and data pins.
    int mosi_pin() const { return mosi_; }
    int miso_pin() const { return miso_; }
    int sck_pin()  const { return sck_;  }
    int csn_pin()  const { return csn_;  }
    int gdo0_pin() const { return gdo0_; }

private:
    void    write_reg(uint8_t addr, uint8_t val);
    uint8_t read_reg(uint8_t addr);
    uint8_t read_status_reg(uint8_t addr);
    void    read_burst(uint8_t addr, uint8_t *data, int len);
    void    strobe(uint8_t cmd);
    void    load_config();
    void    enter_rx();

    spi_device_handle_t spi_       {nullptr};
    int    mosi_, miso_, sck_, csn_, gdo0_;

    static const char * const TAG;
};

#endif // CC1101_RECEIVER
