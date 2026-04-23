// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf.
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "esphome/core/defines.h"
#ifdef CC1101_RECEIVER

#include "cc1101.h"
#include "esp_attr.h"

// ---------------------------------------------------------------------------
// Register configuration for ~345 MHz OOK, Asynchronous Serial Mode.
//
// The CC1101 is configured in Asynchronous Serial Mode (PKTCTRL0=0x32).
// In this mode the FIFO is bypassed: GDO0 (IOCFG0=0x0D) outputs the raw
// demodulated OOK signal in real-time.  The ESP32 RMT peripheral captures
// pulse widths on GDO0; honeywell345_parse() converts them to chips (~136 µs
// empirical chip period) and performs software Manchester decode + CRC-16.
//
// Hardware Manchester is DISABLED — the solid-carrier Honeywell preamble
// provides no transitions for the hardware decoder to align on.
//
// Because the output is the raw demodulator signal, not a resampled bitstream,
// the DRATE register setting does not affect what appears on GDO0.  BSCFG is
// disabled (0x00) for the same reason — bit synchronisation is irrelevant
// in async mode.
//
// Key parameters:
//   Frequency:    ~344.985 MHz  (FREQ2/1/0 = 0x0D/0x44/0xC6 = 869574)
//                 f = 869574 × 26e6 / 2^16 ≈ 344,985 kHz
//   Modulation:   OOK  (MDMCFG2[6:4] = 011)
//   Mode:         Asynchronous Serial (PKTCTRL0[5:4] = 11); FIFO bypassed
//   GDO0:         Raw demodulated OOK output (IOCFG0=0x0D)
//   Channel BW:   ~203 kHz  (MDMCFG4=0x87; CHANBW_E=2, CHANBW_M=0)
//                 BW = 26e6 / (8 × 4 × 4) ≈ 203 kHz
//   Sync word:    none (MDMCFG2[2:0]=000); packet boundaries found by CRC
//   Noise reject: RSSI gating in receive task (threshold -87 dBm)
//                 + RMT glitch filter (signal_range_min_ns = 3 µs)
// ---------------------------------------------------------------------------
struct RegVal { uint8_t addr; uint8_t val; };

static const RegVal kConfig[] = {
    // GDO2: safe default, not connected to ESP32.
    { CC1101Reg::IOCFG2,   0x29 },
    // GDO0 = 0x0D: Asynchronous Serial Data output.
    // In async serial mode the CC1101 drives GDO0 directly from the OOK
    // demodulator — high while carrier is present, low otherwise.
    // The RMT peripheral captures the resulting pulse widths.
    { CC1101Reg::IOCFG0,   0x0D },

    // FIFOTHR: unused in Asynchronous Serial Mode (FIFO is bypassed).
    // Written as a safe default; has no effect on async output.
    { CC1101Reg::FIFOTHR,  0x0C },

    // No sync word — packet boundary found by CRC-16 sliding window in software.
    { CC1101Reg::SYNC1,    0x00 },
    { CC1101Reg::SYNC0,    0x00 },

    // PKTLEN unused in infinite packet mode; set to 0xFF as a safe default.
    { CC1101Reg::PKTLEN,   0xFF },

    // Packet control: no status append, no address check.
    { CC1101Reg::PKTCTRL1, 0x00 },
    // PKTCTRL0=0x32: Asynchronous Serial Mode (bits[5:4]=11), no whitening.
    // FIFO is bypassed; GDO0 outputs the raw demodulated OOK signal.
    { CC1101Reg::PKTCTRL0, 0x32 },

    // Frequency synthesizer control
    { CC1101Reg::FSCTRL1,  0x06 },
    // FSCTRL0: Frequency offset — 0x00 means no additional offset applied.
    // The carrier frequency is tuned directly via FREQ registers below.
    { CC1101Reg::FSCTRL0,  0x00 },

    // Carrier frequency: ~344.985 MHz (26 MHz XTAL).
    // FREQ_word = 0x0D44C6 = 869574
    // f = 869574 × 26e6 / 2^16 ≈ 344,985 kHz (~15 kHz below nominal 345 MHz)
    { CC1101Reg::FREQ2,    0x0D },
    { CC1101Reg::FREQ1,    0x44 },
    { CC1101Reg::FREQ0,    0xC6 },

    // Modem config
    // MDMCFG4=0x87: CHANBW_E=2, CHANBW_M=0 → BW = 26e6/(8×4×4) ≈ 203 kHz
    //               DRATE_E=7 (chip rate register; does not affect async output)
    { CC1101Reg::MDMCFG4,  0x87 },
    // MDMCFG3=0x83: DRATE_M=131 (does not affect async output)
    { CC1101Reg::MDMCFG3,  0x83 },
    // MDMCFG2: OOK modulation, hardware Manchester OFF, no sync word detection.
    // bit[6:4]=011 (OOK), bit[3]=0 (Manchester OFF), bit[2:0]=000 (no sync)
    { CC1101Reg::MDMCFG2,  0x30 },
    // MDMCFG1: 4 preamble bytes, channel spacing exponent
    { CC1101Reg::MDMCFG1,  0x22 },
    { CC1101Reg::MDMCFG0,  0xF8 },

    // Frequency deviation: not applicable for OOK; set to 0.
    { CC1101Reg::DEVIATN,   0x00 },

    // Main radio control state machine: stay in RX after packet received.
    { CC1101Reg::MCSM1,    0x3F },
    { CC1101Reg::MCSM0,    0x18 },

    // Frequency offset compensation — not applicable for OOK.
    { CC1101Reg::FOCCFG,   0x00 },
    // Bit synchronisation — disabled (0x00) in Asynchronous Serial Mode.
    // BSCFG only affects the resampled FIFO bitstream, not the async output.
    { CC1101Reg::BSCFG,    0x00 },

    // AGC control.
    // AGCCTRL2=0x07: MAX_DVGA_GAIN=00 (full DVGA range),
    //   MAX_LNA_GAIN=000 (maximum possible LNA gain),
    //   MAGN_TARGET=111 (42 dB target amplitude).
    // AGCCTRL1=0x00: relative carrier sense threshold disabled,
    //   LNA gain decreased first.
    // AGCCTRL0=0x91: HYST_LEVEL=10 (medium, ~3 dB), WAIT_TIME=01 (16 samples),
    //   AGC_FREEZE=00 (never freeze), FILTER_LENGTH=01 (16 samples for OOK/ASK).
    { CC1101Reg::AGCCTRL2, 0x07 },
    { CC1101Reg::AGCCTRL1, 0x00 },
    { CC1101Reg::AGCCTRL0, 0x91 },

    // Front-end
    { CC1101Reg::FREND1,   0xB6 },
    { CC1101Reg::FREND0,   0x10 },

    // Frequency synthesizer calibration (SmartRF Studio values)
    { CC1101Reg::FSCAL3,   0xE9 },
    { CC1101Reg::FSCAL2,   0x2A },
    { CC1101Reg::FSCAL1,   0x00 },
    { CC1101Reg::FSCAL0,   0x1F },

    // Test registers (recommended values from datasheet)
    { CC1101Reg::TEST2,    0x81 },
    { CC1101Reg::TEST1,    0x35 },
    { CC1101Reg::TEST0,    0x09 },
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CC1101::CC1101(int mosi, int miso, int sck, int csn, int gdo0, spi_host_device_t spi_host)
    : spi_host_(spi_host), mosi_(mosi), miso_(miso), sck_(sck), csn_(csn), gdo0_(gdo0)
{}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

bool CC1101::begin()
{
    // SPI2_HOST is used here; ESPHome's W5500 Ethernet component defaults to
    // SPI3_HOST, so these two do not conflict on boards like the LilyGO T-ETH Lite S3.
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num   = mosi_;
    buscfg.miso_io_num   = miso_;
    buscfg.sclk_io_num   = sck_;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 32;

    esp_err_t ret = spi_bus_initialize(spi_host_, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        // ESP_ERR_INVALID_STATE means the bus is already initialised — acceptable.
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return false;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.mode           = 0;          // SPI mode 0 (CPOL=0, CPHA=0)
    devcfg.clock_speed_hz = 4000000;    // 4 MHz (CC1101 max is 10 MHz)
    devcfg.spics_io_num   = csn_;
    devcfg.queue_size     = 1;

    ret = spi_bus_add_device(spi_host_, &devcfg, &spi_);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure GDO0 as input.  The falling-edge interrupt is armed separately
    // via install_isr() once the receive task handle is available.
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << gdo0_;
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io);

    // Reset the chip and verify it responds.
    strobe(CC1101Strobe::SRES);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t version = read_status_reg(CC1101Status::VERSION);
    if (version == 0x00 || version == 0xFF) 
    {
        ESP_LOGE(TAG, "CC1101 not responding — VERSION=0x%02X (check wiring)", version);
        return false;
    }
    ESP_LOGI(TAG, "CC1101 found: VERSION=0x%02X", version);

    load_config();
    // Verify key registers were written correctly.
    uint8_t mdmcfg4 = read_reg(CC1101Reg::MDMCFG4);
    uint8_t mdmcfg3 = read_reg(CC1101Reg::MDMCFG3);
    uint8_t mdmcfg2 = read_reg(CC1101Reg::MDMCFG2);
    uint8_t bscfg   = read_reg(CC1101Reg::BSCFG);
    uint8_t agcctrl0 = read_reg(CC1101Reg::AGCCTRL0);
    uint8_t agcctrl2 = read_reg(CC1101Reg::AGCCTRL2);
    uint8_t fsctrl0 = read_reg(CC1101Reg::FSCTRL0);
    ESP_LOGD(TAG, "MDMCFG4=0x%02X (want 0x87)  MDMCFG3=0x%02X (want 0x83)  MDMCFG2=0x%02X (want 0x30)  BSCFG=0x%02X (want 0x00)  AGCCTRL0=0x%02X (want 0x91)  AGCCTRL2=0x%02X (want 0x07)  FSCTRL0=0x%02X (want 0x00)",
             mdmcfg4, mdmcfg3, mdmcfg2, bscfg, agcctrl0, agcctrl2, fsctrl0);
    enter_rx();
    ESP_LOGI(TAG, "CC1101 RX started at ~344.985 MHz (203 kHz BW, async OOK)");
    return true;
}

void CC1101::load_config()
{
    for (const auto &rv : kConfig)
        write_reg(rv.addr, rv.val);
}

void CC1101::enter_rx()
{
    strobe(CC1101Strobe::SIDLE);
    strobe(CC1101Strobe::SFRX);
    strobe(CC1101Strobe::SRX);
}

int8_t CC1101::rssi_now()
{
    uint8_t rssi_raw = read_status_reg(CC1101Status::RSSI);
    // CC1101 datasheet section 17.3: RSSI_offset = 74
    // if raw >= 128: dBm = (raw - 256) / 2 - 74
    // if raw <  128: dBm = raw / 2 - 74
    if (rssi_raw >= 128)
        return static_cast<int8_t>((static_cast<int>(rssi_raw) - 256) / 2 - 74);
    return static_cast<int8_t>(rssi_raw / 2 - 74);
}

// ---------------------------------------------------------------------------
// SPI primitives
// ---------------------------------------------------------------------------

void CC1101::write_reg(uint8_t addr, uint8_t val)
{
    uint8_t tx[2] = { static_cast<uint8_t>(addr & 0x3F), val };
    spi_transaction_t t = {};
    t.length    = 16;
    t.tx_buffer = tx;
    spi_device_transmit(spi_, &t);
}

uint8_t CC1101::read_reg(uint8_t addr)
{
    uint8_t tx[2] = { static_cast<uint8_t>((addr & 0x3F) | CC1101_READ_BIT), 0x00 };
    uint8_t rx[2] = {};
    spi_transaction_t t = {};
    t.length    = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_transmit(spi_, &t);
    return rx[1];
}

uint8_t CC1101::read_status_reg(uint8_t addr)
{
    // Status registers are read with the burst bit set (0xC0 prefix).
    uint8_t tx[2] = { static_cast<uint8_t>(addr | CC1101_READ_BURST), 0x00 };
    uint8_t rx[2] = {};
    spi_transaction_t t = {};
    t.length    = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_transmit(spi_, &t);
    return rx[1];
}

void CC1101::strobe(uint8_t cmd)
{
    uint8_t tx = cmd;
    spi_transaction_t t = {};
    t.length    = 8;
    t.tx_buffer = &tx;
    spi_device_transmit(spi_, &t);
    // Short settle time after reset strobe.
    if (cmd == CC1101Strobe::SRES)
        vTaskDelay(pdMS_TO_TICKS(5));
}

#endif // CC1101_RECEIVER
