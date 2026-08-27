// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include "vista_bus.h"
#include "zone_manager.h"
#include "partition_manager.h"
#include "aui_manager.h"
#include "command_writer.h"
#include "helper_structs.h"
#include "helper_enums.h"
#include "constants.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <cstdint>

namespace esphome {
namespace alarm_panel {
class vistaECPTextSensor;

// -----------------------------------------------------------------------
// PacketDispatcher
//
// Reads one packet at a time from VistaBus and routes it to the
// appropriate collaborator (PartitionManager, ZoneManager, AUIManager,
// or a sensor callback).
//
// Owns:
//   - The raw F7 / legacy-SE decode logic (StatusFlags assembly).
//   - The F9 LRR message formatting logic.
//   - The legacy SE multi-packet assembly state machine.
//
// Does NOT own:
//   - VistaBus (received by reference in constructor).
//   - Any of the collaborator objects (held as pointers in Config).
//
// Lifecycle:
//   1. Construct with a fully-configured VistaBus and a populated Config.
//   2. Call dispatch_one() each iteration of the processReceiveQueue loop.
// -----------------------------------------------------------------------
class PacketDispatcher {
 public:
  struct Config {
    ZoneManager *zones{nullptr};
    PartitionManager *partitions{nullptr};
    AUIManager *aui{nullptr};
    CommandWriter *cmd{nullptr};
    vistaECPTextSensor *lrr_sensor{nullptr};
    vistaECPTextSensor *rf_sensor{nullptr};
    text_sensor::TextSensor *chksum_fail_sensor{nullptr};
    time::RealTimeClock *rtc{nullptr};
    int64_t ttl{0};
  };

  PacketDispatcher(VistaBus &bus, const Config &cfg);

  // Read one packet from VistaBus and dispatch to the appropriate
  // collaborator.  Blocks internally in VistaBus::read_packet() until
  // a packet or timeout occurs.
  void dispatch_one();

 private:
  // -------------------------------------------------------------------
  // F7 / legacy-SE decode
  //
  // Translates the raw 48-byte panel status frame into a StatusFlags
  // value-object.  Performs the partition lookup against the list
  // returned by PartitionManager::partitions() and applies extended-
  // character (>0x7F) UTF-8 conversion to the two keypad prompt strings.
  // -------------------------------------------------------------------
  StatusFlags decode_status_flags(const char *cbuf, int size);

  // Maximum characters translate_prompt() will write to 'out', excluding
  // the null terminator.  StatusFlags::prompt1/prompt2 are char[32], and
  // a line of 16 extended characters expands to 32 UTF-8 bytes, so the
  // output is capped at 31 to leave room for the terminator.
  static constexpr size_t kPromptOutChars = 31;

  // Translate a 16-byte raw prompt line into a null-terminated UTF-8
  // string in 'out' (kPromptOutChars + 1 bytes minimum).  'first_byte'
  // allows the caller to override src[0] (used to strip the backlight
  // bit from line 1).  Extended characters (>0x7F) expand to two bytes,
  // so the result may be longer than 16 characters.
  static void translate_prompt(const char *src, char first_byte, char *out);

  // -------------------------------------------------------------------
  // Legacy SE multi-packet assembly
  //
  // The Vista20 SE protocol sends status as a sequence of small packets
  // that must be assembled into a single F7-compatible buffer before
  // decode_status_flags() can be called.
  //
  // Returns true when a complete frame has been assembled (index == 8).
  // The assembled frame is held in legacy_cmd_buffer_.
  // -------------------------------------------------------------------
  bool assemble_legacy_se(const char *payload, int size);

  // -------------------------------------------------------------------
  // F9 LRR packet handler
  //
  // Decodes the Contact-ID fields from the raw F9 bytes, builds the
  // human-readable "CID_q ccc: description on/by zone/user, Partition p"
  // string, and publishes it to cfg_.lrr_sensor.
  // -------------------------------------------------------------------
  void handle_lrr_packet(const char *payload, int size);

  // -------------------------------------------------------------------
  // Packet logging
  //
  // Logs the raw packet bytes at ESP_LOGD level (or ESP_LOGE on checksum
  // failure).  Truncates to 8 bytes when DEBUG_LOG is not defined.
  // -------------------------------------------------------------------
  void print_packet(const char *cbuf, int type, int src, int len);

  // -------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------

  VistaBus &bus_;
  Config cfg_;

  // Cumulative checksum failure counter — incremented for source==0xCF
  // packets (panel-direction failures) and empty returns from
  // on_rf_zone_packet (green-wire RF data failures).
  // Record one checksum failure, unless the device is still inside the
  // post-boot settle window -- see kChksumSettleUs.  'what' names the
  // source for the log line.
  void note_chksum_failure(const char *what);

  uint32_t chksum_failures_{0};
  uint32_t chksum_last_reported_{0};

  // Legacy SE multi-packet assembly state.
  uint8_t legacy_packet_index_{0};
  char legacy_cmd_buffer_[48]{};
};

}  // namespace alarm_panel
}  // namespace esphome
