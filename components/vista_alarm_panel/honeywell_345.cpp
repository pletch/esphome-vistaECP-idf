// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf.
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "honeywell_345.h"
#include <vector>

#ifdef CC1101_RECEIVER

static const char *TAG = "honeywell345";

// ---------------------------------------------------------------------------
// Standard CRC-16 (Left-Shifting) — matches rtl_433 protocol 70.
//   Standard poly: 0x8005 (Honeywell 5800-series)
//   2GIG poly:     0x8050
// ---------------------------------------------------------------------------
static uint16_t crc16(const uint8_t *data, int len, uint16_t poly)
{
    uint16_t crc = 0x0000;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
        }
    }
    return crc;
}

HoneywellPacket honeywell345_parse(const rmt_symbol_word_t *symbols, size_t num_symbols)
{
    HoneywellPacket pkt{};
    // 104 chips (sync nibble + payload) can result in as few as 26-52 RMT symbols.
    if (num_symbols < 30) return pkt;

    // Convert RMT symbol durations into a chip bitstream.
    // The chip clock is estimated adaptively from the preamble below; 136 µs is the
    // nominal fallback used when insufficient preamble samples are available.
    std::vector<uint8_t> chips;
    chips.reserve(num_symbols * 2 + 2);

    // If the capture starts with a High signal (level 1), it indicates the RMT 
    // hardware triggered on the first rising edge, missing the initial Low chip 
    // of the Manchester '01' sequence. Prepend a single Low chip to restore phase.
    if (num_symbols > 0 && symbols[0].level0 == 1) 
    {
        chips.push_back(0);
    }

    // Estimate the chip width from individual short pulses in the preamble.
    // We sample each half-symbol independently and keep only those that fall in the
    // range of a plausible single chip (70–210 µs covers ±50% of the 136 µs nominal).
    // Averaging these avoids the bias that paired-symbol averaging introduced when
    // any preamble symbol happened to contain a 2-chip half.
    float tuned_clock = 136.0f;
    uint32_t preamble_sum = 0;
    int preamble_count = 0;
    for (size_t i = 1; i < 16 && i < num_symbols; i++) 
    {
        if (symbols[i].duration0 > 70 && symbols[i].duration0 < 210) 
        {
            preamble_sum += symbols[i].duration0;
            preamble_count++;
        }
        if (symbols[i].duration1 > 70 && symbols[i].duration1 < 210) 
        {
            preamble_sum += symbols[i].duration1;
            preamble_count++;
        }
    }
    if (preamble_count >= 6) 
    {
        tuned_clock = (float)preamble_sum / preamble_count;
        ESP_LOGV(TAG, "Tuned chip clock: %.2f us (from %d samples)", tuned_clock, preamble_count);
    }

    // Convert each RMT duration to chips using a per-pulse binary threshold
    //
    // At this baud rate a single RMT segment is ALWAYS either 1 chip or 2 chips —
    // never more.  Classifying each duration independently against a 1.5× threshold
    // means errors are bounded to a single segment. 
    const uint32_t threshold = (uint32_t)(tuned_clock * 1.5f + 0.5f);
    for (size_t i = 0; i < num_symbols; i++) 
    {
        int n0 = (symbols[i].duration0 >= threshold) ? 2 : 1;
        for (int j = 0; j < n0; j++) chips.push_back(symbols[i].level0);

        int n1 = (symbols[i].duration1 >= threshold) ? 2 : 1;
        for (int j = 0; j < n1; j++) chips.push_back(symbols[i].level1);
    }

    // If the resulting chip count is odd, append a trailing zero chip to ensure 
    // the sliding window pairs remain aligned for the final bit of the payload.
    if (chips.size() % 2 != 0) 
    {
        chips.push_back(0);
    }

    // Minimum chips required: 8 (sync nibble 'E') + 96 (6-byte payload) = 104.
    if (chips.size() < 104) 
    {
        ESP_LOGV(TAG, "Insufficient chips (%zu), ignoring burst", chips.size());
        return pkt;
    }
    ESP_LOGD(TAG, "Processing %zu chips...", chips.size());

    // Log raw chip bitstream for debugging (up to 256 chips)
    size_t to_print = chips.size() > 256 ? 256 : chips.size();
    char raw_str[257];
    for (size_t k = 0; k < to_print; k++) raw_str[k] = chips[k] ? '1' : '0';
    raw_str[to_print] = '\0';
    ESP_LOGD(TAG, "Raw chip bitstream: %s", raw_str);

    // Sliding window: try to decode Manchester from every possible chip offset.
    // We scan for the 'E' nibble (01010110) then validate the 96-chip payload following it.
    for (size_t i = 0; i + 104 <= chips.size(); /* i incremented inside */) 
    {
            // Check for Manchester 'E' nibble (1110) -> 01 01 01 10
            if (chips[i] != 0 || chips[i+1] != 1 || chips[i+2] != 0 || chips[i+3] != 1 ||
                chips[i+4] != 0 || chips[i+5] != 1 || chips[i+6] != 1 || chips[i+7] != 0) 
            {
                i++;
                continue;
            }

            uint8_t decoded[6] = {0};
            bool valid_manchester = true;
            size_t violation_pos = 0;

            // Decode 48 bits (96 chips) starting after the 'E' nibble
            for (int bit_idx = 0; bit_idx < 48; bit_idx++) 
            {
                size_t pos = i + 8 + (bit_idx * 2);
                
                uint8_t c0 = chips[pos];
                uint8_t c1 = chips[pos+1];

                if (c0 == c1) { 
                    valid_manchester = false; 
                    violation_pos = bit_idx;
                    break; 
                }
                
                // Standard Manchester (IEEE 802.3): 01 = 1, 10 = 0 (MSB-first)
                if (c0 == 0) decoded[bit_idx / 8] |= (1 << (7 - (bit_idx % 8)));

            }

            if (valid_manchester) 
            {
                uint16_t crc_rx = ((uint16_t)decoded[4] << 8) | decoded[5];
                uint16_t crc_rx_le = ((uint16_t)decoded[5] << 8) | decoded[4];

                uint16_t crc_calc_h = crc16(decoded, 4, 0x8005);
                uint16_t crc_calc_2g = crc16(decoded, 4, 0x8050);

                bool match = (crc_calc_h == crc_rx || crc_calc_2g == crc_rx || 
                              crc_calc_h == crc_rx_le || crc_calc_2g == crc_rx_le);

                // Handle common Honeywell 5800 series characteristic: some models 
                // zero out the last nibble of the 16-bit CRC field (12-bit CRC).
                if (!match && (crc_rx & 0x000F) == 0) 
                {
                    if ((crc_calc_h & 0xFFF0) == crc_rx || (crc_calc_2g & 0xFFF0) == crc_rx) 
                    {
                        match = true;
                    }
                }

                if (match) {
                    
                    // Assemble 20-bit serial per Honeywell 5800 spec:
                    // Byte 0 [3:0] (high bits), Byte 1, Byte 2 (low bits)
                    pkt.serial = (uint32_t)(decoded[0] & 0x0F) << 16 | 
                                 (uint32_t)decoded[1] << 8 | decoded[2];
                    
                    uint8_t ev = decoded[3];
                    uint8_t ecp = 0;
                    if (ev & 0x80) ecp |= 0x80; // Loop 1
                    if (ev & 0x40) ecp |= 0x40; // Loop 4
                    if (ev & 0x20) ecp |= 0x20; // Loop 2
                    if (ev & 0x10) ecp |= 0x10; // Loop 3
                    if (ev & 0x08) ecp |= 0x02;
                    if (ev & 0x04) ecp |= 0x04;
                    
                    pkt.ecp_status = ecp;
                    pkt.heartbeat = (ev & 0x04) != 0;
                    pkt.valid = true;

                    return pkt;
                }
                else 
                {
                    ESP_LOGV(TAG, "CRC Mismatch at chip %zu: RX=0x%04X, Calc(H)=0x%04X, Calc(2G)=0x%04X [Data: %02X %02X %02X %02X]", 
                             i, crc_rx, crc_calc_h, crc_calc_2g, decoded[0], decoded[1], decoded[2], decoded[3]);
                }

                // If the sequence was Manchester-valid, the next 
                // possible start is at least 2 chips away.
                i += 2;
            } 
            else 
            {
                // If Manchester failed at bit_idx, we could technically skip 
                // ahead, but for OOK jitter, a simple shift of 1 is safest.
                i++;
            }
    }
    return pkt;
}

#endif // CC1101_RECEIVER
