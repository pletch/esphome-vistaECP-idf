// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#pragma once

#include "esphome/components/text_sensor/text_sensor.h"
#include "../vista_alarm.h"

namespace esphome::alarm_panel {

class VistaTextSensor : public text_sensor::TextSensor, public VistaEcpTextSensor, public Parented<VistaESPHome> {
 public:
  void process(const std::string &text) override;

 protected:
};

}  // namespace esphome::alarm_panel
