// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include <string>

namespace esphome::alarm_panel {
class VistaEcpBinarySensor {
 public:
  virtual void process(bool triggered) = 0;
  virtual ~VistaEcpBinarySensor() = default;
};

class VistaEcpTextSensor {
 public:
  virtual void process(const std::string &text) = 0;
  virtual ~VistaEcpTextSensor() = default;
};

}  // namespace esphome::alarm_panel
