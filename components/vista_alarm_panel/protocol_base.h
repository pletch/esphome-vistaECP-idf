// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include "ecp_protocol.h"
#include "vista_bus.h"

template<typename ProtocolType> class ProtocolBase : public VistaECP {
  ProtocolBase() = default;

 public:
  using VistaECP::VistaECP;
  void check_send_q(SendPacket &pkt) { static_cast<ProtocolType *>(this)->check_send_q_impl(pkt); }

  int handle_uart_events(const SendPacket &pkt_to_send, uint8_t *buf) {
    return static_cast<ProtocolType *>(this)->handle_uart_events_impl(pkt_to_send, buf);
  }

  int monitor_task_sync(uint8_t *buf, uint32_t &val) {
    return static_cast<ProtocolType *>(this)->monitor_task_sync_impl(buf, val);
  }

  void quick_decode_fa(const char *cbuf) { static_cast<ProtocolType *>(this)->quick_decode_fa_impl(cbuf); }

  void dispatch_fa() { static_cast<ProtocolType *>(this)->dispatch_fa_impl(); }

 private:
  friend ProtocolType;
};
