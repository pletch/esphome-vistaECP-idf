// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "../vista_alarm.h"

namespace esphome {
namespace alarm_panel {

class VistaBinarySensor : public binary_sensor::BinarySensor,
                          public vistaECPBinarySensor,
                          public Parented<VistaESPHome> {
 public:
  void process(bool triggered) override;

 protected:
};

}  // namespace alarm_panel
}  // namespace esphome
