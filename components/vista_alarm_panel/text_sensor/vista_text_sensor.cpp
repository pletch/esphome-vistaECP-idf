// Copyright (C) 2020 Alain Turbide
// Copyright (C) 2025-2026 Tim Pletcher
//
// This file is part of esphome-vistaECP-idf, derived from esphome-vistaECP
// (https://github.com/Dilbert66/esphome-vistaECP).
//
// Licensed under the GNU Lesser General Public License v2.1.
// See COPYING.LESSER in the project root for details.

#include "vista_text_sensor.h"

namespace esphome {
namespace alarm_panel {

void VistaTextSensor::process(const std::string &text) { this->publish_state(text); }

}  // namespace alarm_panel
}  // namespace esphome
